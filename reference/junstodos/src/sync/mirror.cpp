#include "mirror.h"

#include <chrono>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "protocol.h"
#include "negentropy.h"
#include "negentropy/storage/Vector.h"

namespace jsync {

// Snapshot of local storage: a sealed negentropy Vector for reconciliation
// plus id -> payload for pushes. Copying (ts, id) pairs out of LMDB means no
// read txn is held across network round-trips.
struct LocalSet {
    negentropy::storage::Vector vec;
    std::unordered_map<std::string, std::string> payload_by_id;

    static LocalSet snapshot(store::Store* s) {
        LocalSet ls;
        store::load_all(s, [&](const store::Item& it) {
            std::string idstr((const char*)it.id.data(), store::ID_SIZE);
            ls.vec.insert(it.updated_at, idstr);
            ls.payload_by_id.emplace(std::move(idstr), it.payload);
        });
        ls.vec.seal();
        return ls;
    }

    std::string fingerprint() {
        return std::string(vec.fingerprint(0, vec.size()).sv());
    }
};

static Stat fetch_stat(const Transport& t) {
    return decode_stat_resp(t(encode_stat_req()));
}

static uint64_t us_now() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// Run the negentropy rounds; returns ids only we have (have) and ids only
// the server has (need).
static void reconcile(LocalSet& ls, const Transport& t,
                      std::vector<std::string>& have, std::vector<std::string>& need,
                      int* rounds) {
    negentropy::Negentropy<negentropy::storage::Vector> ne(ls.vec);
    std::string msg = ne.initiate();
    while (true) {
        (*rounds)++;
        std::string resp = t(encode_neg_req(msg));
        auto next = ne.reconcile(resp, have, need);
        if (!next.has_value()) break;
        msg = *next;
    }
}

MirrorStats mirror_up(store::Store* s, const Transport& transport, bool allow_wipe) {
    MirrorStats stats;
    uint64_t t0 = us_now();
    LocalSet ls = LocalSet::snapshot(s);
    stats.snapshot_us = us_now() - t0;

    if (ls.vec.size() == 0 && !allow_wipe) {
        Stat st = fetch_stat(transport);
        if (st.count > 0) {
            throw std::runtime_error(
                "mirror_up: local store is empty but server is not; refusing to "
                "wipe the backup (pass allow_wipe to override)");
        }
    }

    std::vector<std::string> have, need;
    uint64_t t1 = us_now();
    reconcile(ls, transport, have, need, &stats.rounds);
    stats.reconcile_us = us_now() - t1;

    std::vector<Record> puts;
    for (const auto& idstr : have) {
        auto it = ls.payload_by_id.find(idstr);
        if (it == ls.payload_by_id.end()) continue;  // vanished mid-sync; next pass heals
        Record rec;
        rec.id = id_from_sv(idstr);
        rec.updated_at = version_ts(rec.id);
        rec.payload = it->second;
        puts.push_back(std::move(rec));
    }
    std::vector<Key> dels;
    for (const auto& idstr : need) {
        Key k;
        k.id = id_from_sv(idstr);
        k.updated_at = version_ts(k.id);
        dels.push_back(k);
    }

    uint64_t t2 = us_now();
    if (!puts.empty() || !dels.empty()) {
        transport(encode_ops_req(puts, dels));
    }
    stats.ops_us = us_now() - t2;
    stats.pushed = puts.size();
    stats.deleted_remote = dels.size();

    uint64_t t3 = us_now();
    Stat st = fetch_stat(transport);
    stats.converged = (st.count == ls.vec.size()) && (st.fingerprint == ls.fingerprint());
    stats.verify_us = us_now() - t3;
    stats.total_us = us_now() - t0;
    return stats;
}

MirrorStats mirror_down(store::Store* s, const Transport& transport) {
    MirrorStats stats;
    LocalSet ls = LocalSet::snapshot(s);

    std::vector<std::string> have, need;
    reconcile(ls, transport, have, need, &stats.rounds);

    // need = server-only records: fetch and insert locally
    std::vector<Key> want;
    for (const auto& idstr : need) {
        Key k;
        k.id = id_from_sv(idstr);
        k.updated_at = version_ts(k.id);
        want.push_back(k);
    }
    std::vector<Record> fetched;
    if (!want.empty()) {
        fetched = decode_records_resp(transport(encode_fetch_req(want)));
    }

    // have = local-only records: delete locally (server is master here)
    if (!fetched.empty() || !have.empty()) {
        store::write(s, [&](store::Writer& w) {
            for (const auto& idstr : have) {
                auto id = id_from_sv(idstr);
                w.del(version_ts(id), id);
            }
            for (const auto& rec : fetched) {
                w.put(rec.updated_at, rec.id, rec.payload);
            }
        });
    }
    stats.fetched = fetched.size();
    stats.deleted_local = have.size();

    // verify against a fresh local snapshot
    LocalSet after = LocalSet::snapshot(s);
    Stat st = fetch_stat(transport);
    stats.converged = (st.count == after.vec.size()) && (st.fingerprint == after.fingerprint());
    return stats;
}

}  // namespace jsync
