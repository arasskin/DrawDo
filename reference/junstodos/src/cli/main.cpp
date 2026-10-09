// jt-cli: a full sync peer in a terminal, for multiuser testing before the
// app has a UI. Each keyfile is an identity (= a namespace on the server).
//
//   jt-cli <base-url> <keyfile> id                 print my pubkey
//   jt-cli <base-url> <keyfile> add <text>         add a todo, mirror up
//   jt-cli <base-url> <keyfile> done <ts>          remove by timestamp, mirror up
//   jt-cli <base-url> <keyfile> ls [pubkey-hex]    list mine, or read a friend's
//   jt-cli <base-url> <keyfile> watch <pk...>      live feed -> persistent friend stores
//   jt-cli <base-url> <keyfile> cached <pk>        read a friend store offline

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../db/store.h"
#include "../sync/auth.h"
#include "../sync/mirror.h"
#include "../sync/net_platform.h"
#include "../sync/protocol.h"
#include "../sync/watch.h"

#include "negentropy.h"
#include "negentropy/storage/Vector.h"

using namespace jsync;

// Read an entire namespace without persisting anything: reconcile from an
// empty set (=> need everything), then FETCH. This is the friend-read path.
static std::vector<Record> read_namespace(const Transport& t) {
    negentropy::storage::Vector empty;
    empty.seal();
    negentropy::Negentropy<negentropy::storage::Vector> ne(empty);
    std::vector<std::string> have, need;
    std::string msg = ne.initiate();
    while (true) {
        std::string resp = t(encode_neg_req(msg));
        auto next = ne.reconcile(resp, have, need);
        if (!next.has_value()) break;
        msg = *next;
    }
    std::vector<Key> want;
    for (const auto& idstr : need) {
        Key k;
        k.id = id_from_sv(idstr);
        k.updated_at = version_ts(k.id);
        want.push_back(k);
    }
    if (want.empty()) return {};
    return decode_records_resp(t(encode_fetch_req(want)));
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <base-url> <keyfile> id\n"
                "       %s <base-url> <keyfile> add <text>\n"
                "       %s <base-url> <keyfile> done <ts>\n"
                "       %s <base-url> <keyfile> ls [pubkey-hex]\n"
                "       %s <base-url> <keyfile> watch [pk...]\n"
                "       %s <base-url> <keyfile> cached <pk>\n",
                argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }
    std::string base = argv[1], keyfile = argv[2], cmd = argv[3];
    std::string url = base + "/sync";

    try {
        Identity me = identity_load_or_create(keyfile);

        if (cmd == "id") {
            printf("%s\n", pubkey_hex(me.pub).c_str());
            return 0;
        }

        if (cmd == "ls") {
            uint8_t ns[PUBKEY_SIZE];
            bool own = (argc < 5);
            if (own) {
                memcpy(ns, me.pub, PUBKEY_SIZE);
            } else if (!pubkey_from_hex(argv[4], ns)) {
                fprintf(stderr, "bad pubkey hex\n");
                return 2;
            }
            // reads need no signer — this IS the open-read model
            auto t = authed_transport(http_transport(url), ns, nullptr);
            auto recs = read_namespace(t);
            printf("%s (%zu items)\n", pubkey_hex(ns).c_str(), recs.size());
            for (const auto& r : recs) {
                printf("  %llu  %.*s\n", (unsigned long long)r.updated_at,
                       (int)r.payload.size(), r.payload.data());
            }
            return 0;
        }

        if (cmd == "cached" && argc >= 5) {
            // friend data persisted by `watch` — readable with zero network
            std::string ns = argv[4];
            auto* fs = store::open(keyfile + ".friends/" + ns + ".db");
            printf("%s (cached, %llu items)\n", ns.c_str(),
                   (unsigned long long)store::item_count(fs));
            store::load_all(fs, [](const store::Item& it) {
                printf("  %llu  %s\n", (unsigned long long)it.updated_at, it.payload.c_str());
            });
            store::close(fs);
            return 0;
        }

        if (cmd == "watch") {
            // cycle: reconcile everything, park on /wait, repeat. Pokes only
            // shorten the park; the cycle-start read is the correctness path.
            std::vector<Pubkey> subs;
            for (int i = 4; i < argc; i++) {
                Pubkey pk;
                if (!pubkey_from_hex(argv[i], pk.data())) {
                    fprintf(stderr, "bad pubkey hex: %s\n", argv[i]);
                    return 2;
                }
                subs.push_back(pk);
            }
            if (subs.empty()) {
                Pubkey own;
                memcpy(own.data(), me.pub, PUBKEY_SIZE);
                subs.push_back(own);
            }
            // friend data is stored as our own, in a store per friend —
            // mirror_down IS the refresh, deletes included. Never mirrored up.
            std::map<std::string, std::string> last;
            for (;;) {
                for (const auto& pk : subs) {
                    std::string ns = pubkey_hex(pk.data());
                    auto t = authed_transport(http_transport(url), pk.data(), nullptr);
                    auto* fs = store::open(keyfile + ".friends/" + ns + ".db");
                    jsync::mirror_down(fs, t);
                    std::string view;
                    store::load_all(fs, [&](const store::Item& it) {
                        view += "  " + std::to_string(it.updated_at) + "  " + it.payload + "\n";
                    });
                    uint64_t count = store::item_count(fs);
                    store::close(fs);
                    if (last[ns] != view) {
                        printf("== %s (%llu items)\n%s", ns.c_str(),
                               (unsigned long long)count, view.c_str());
                        fflush(stdout);
                        last[ns] = view;
                    }
                }
                jsync::wait_for_changes(base + "/wait", subs);  // poke or ~50s timeout
            }
        }

        // mutating commands: local store lives next to the keyfile
        auto* s = store::open(keyfile + ".db");
        auto t = authed_transport(http_transport(url), me.pub, &me);

        if (cmd == "add" && argc >= 5) {
            uint64_t ts = now_ms();
            uint8_t uuid[24];
            arc4random_buf(uuid, sizeof uuid);
            store::Id id = make_version_id(ts, uuid);
            store::write(s, [&](store::Writer& w) { w.put(ts, id, argv[4]); });
        } else if (cmd == "done" && argc >= 5) {
            uint64_t ts = strtoull(argv[4], nullptr, 10);
            bool found = false;
            store::load_all(s, [&](const store::Item& it) {
                if (it.updated_at == ts) {
                    store::write(s, [&](store::Writer& w) { w.del(it.updated_at, it.id); });
                    found = true;
                }
            });
            if (!found) {
                fprintf(stderr, "no item with ts %llu\n", (unsigned long long)ts);
                return 1;
            }
        } else if (cmd != "sync") {
            fprintf(stderr, "unknown command: %s\n", cmd.c_str());
            return 2;
        }

        auto st = mirror_up(s, t);
        store::close(s);
        printf("%s: %s (pushed=%zu deleted=%zu rounds=%d)\n", cmd.c_str(),
               st.converged ? "converged" : "NOT CONVERGED",
               st.pushed, st.deleted_remote, st.rounds);
        return st.converged ? 0 : 1;
    } catch (const std::exception& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
