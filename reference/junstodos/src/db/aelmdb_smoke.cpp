// Phase 0 smoke test for the AELMDB + negentropy stack.
// 1. Open an AELMDB env, create a dbi with aggregate flags + hash offset 8.
// 2. Insert N (timestamp, 32-byte id) keys.
// 3. Insert the same items into negentropy's reference Vector storage.
// 4. Verify fingerprints agree across backends (protocol compatibility).
// 5. Remove one item from the Vector side and run a full negentropy
//    reconciliation between the two backends; expect exactly that diff.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "lmdbxx/lmdb++.h"
#include "negentropy.h"
#include "negentropy/storage/SliceAELMDB.h"
#include "negentropy/storage/Vector.h"

static std::string make_id(uint64_t n) {
    // deterministic 32-byte pseudo-id
    std::string id(32, '\0');
    for (int i = 0; i < 32; i++) id[i] = (char)((n * 31 + i * 131) & 0xFF);
    return id;
}

static std::string make_key(uint64_t ts, const std::string& id) {
    std::string k(8 + 32, '\0');
    for (int i = 0; i < 8; i++) k[i] = (char)((ts >> (8 * (7 - i))) & 0xFF);
    memcpy(&k[8], id.data(), 32);
    return k;
}

int smoke_main(const char* db_dir) {
    const int N = 1000;
    std::filesystem::remove_all(db_dir);
    std::filesystem::create_directories(db_dir);

    auto env = lmdb::env::create();
    env.set_max_dbs(8);
    env.set_mapsize(64ull * 1024 * 1024);
    env.open(db_dir, 0);

    lmdb::dbi dbi{0};
    {
        // setup txn: create dbi + configure hash offset on the EMPTY dbi
        auto txn = lmdb::txn::begin(env);
        dbi = lmdb::dbi::open(txn, "sync",
            MDB_CREATE | MDB_AGG_ENTRIES | MDB_AGG_HASHSUM | MDB_AGG_HASHSOURCE_FROM_KEY);
        dbi.set_hash_offset(txn, 8);
        txn.commit();
    }
    {
        auto txn = lmdb::txn::begin(env);
        // AELMDB wart: set_hash_offset persists, but the setting txn's dbi
        // handle keeps stale cached metadata. Reopen to refresh.
        dbi = lmdb::dbi::open(txn, "sync");
        for (uint64_t i = 0; i < N; i++) {
            auto k = make_key(1000 + i, make_id(i));
            dbi.put(txn, std::string_view(k), std::string_view{});
        }
        txn.commit();
    }

    negentropy::storage::Vector vec;
    for (uint64_t i = 0; i < N; i++) {
        if (i == 42) continue;  // the deliberate diff
        vec.insert(1000 + i, make_id(i));
    }
    vec.seal();

    auto rtxn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
    // reopen per-txn so the fork's cached DB metadata (hash offset) is fresh
    dbi = lmdb::dbi::open(rtxn, "sync");
    negentropy::storage::WholeAELMDB whole(rtxn, dbi);

    printf("sizes: aelmdb=%llu vector=%llu\n",
           (unsigned long long)whole.size(), (unsigned long long)vec.size());
    if (whole.size() != (uint64_t)N) { printf("FAIL: aelmdb size\n"); return 1; }

    // fingerprint compatibility: same items must fingerprint identically
    {
        negentropy::storage::Vector vec_full;
        for (uint64_t i = 0; i < N; i++) vec_full.insert(1000 + i, make_id(i));
        vec_full.seal();
        auto fp_a = whole.fingerprint(0, whole.size());
        auto fp_v = vec_full.fingerprint(0, vec_full.size());
        if (fp_a.sv() != fp_v.sv()) { printf("FAIL: fingerprint mismatch across backends\n"); return 1; }
        printf("fingerprints match across backends\n");
    }

    // full reconciliation: Vector (client, missing item 42) vs AELMDB (server)
    negentropy::Negentropy<negentropy::storage::Vector> client(vec, 4096);
    negentropy::Negentropy<negentropy::storage::WholeAELMDB> server(whole, 4096);

    std::string msg = client.initiate();
    std::vector<std::string> have, need;
    int rounds = 0;
    while (true) {
        rounds++;
        std::string resp = server.reconcile(msg);
        auto next = client.reconcile(resp, have, need);
        if (!next.has_value()) break;
        msg = *next;
    }

    printf("reconcile: rounds=%d have=%zu need=%zu\n", rounds, have.size(), need.size());
    if (have.size() != 0 || need.size() != 1) { printf("FAIL: wrong diff\n"); return 1; }
    if (need[0] != make_id(42)) { printf("FAIL: wrong needed id\n"); return 1; }

    printf("OK: aelmdb+negentropy smoke test passed\n");
    return 0;
}

#ifdef SMOKE_HOST_MAIN
int main() { return smoke_main("/tmp/aelmdb-smoke-db"); }
#endif
