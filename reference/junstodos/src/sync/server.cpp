#include "server.h"

#include <stdexcept>

#include "protocol.h"
#include "../db/store_sync.h"

namespace jsync {

std::string sync_handle(store::Store* s, std::string_view request) {
    switch (req_type(request)) {
        case NEG: {
            // One reconciliation round against the live tree. The AELMDB
            // aggregates make fingerprinting O(log n) — this is the call
            // the whole storage design exists for.
            std::string out;
            store::with_sync_view(s, [&](negentropy::storage::WholeAELMDB& view) {
                negentropy::Negentropy<negentropy::storage::WholeAELMDB> ne(view);
                out = ne.reconcile(decode_neg_req(request));
            });
            return out;
        }
        case OPS: {
            // Mirror ops from the master, applied verbatim in one txn.
            std::vector<Record> puts;
            std::vector<Key> dels;
            decode_ops_req(request, &puts, &dels);
            store::write(s, [&](store::Writer& w) {
                for (const auto& p : puts) w.put(p.updated_at, p.id, p.payload);
                for (const auto& d : dels) w.del(d.updated_at, d.id);
            });
            return std::string(1, '\x01');
        }
        case STAT: {
            std::string out;
            store::with_sync_view(s, [&](negentropy::storage::WholeAELMDB& view) {
                uint64_t n = view.size();
                auto fp = view.fingerprint(0, n);
                out = encode_stat_resp(n, fp.sv());
            });
            return out;
        }
        case FETCH: {
            std::vector<Record> recs;
            for (const auto& k : decode_fetch_req(request)) {
                Record rec;
                rec.updated_at = k.updated_at;
                rec.id = k.id;
                // absent keys are skipped: the set changed since reconcile;
                // the next mirror pass converges on the new state
                if (store::get(s, k.updated_at, k.id, &rec.payload)) {
                    recs.push_back(std::move(rec));
                }
            }
            return encode_records_resp(recs);
        }
        default:
            throw std::runtime_error("sync: unknown request type");
    }
}

}  // namespace jsync
