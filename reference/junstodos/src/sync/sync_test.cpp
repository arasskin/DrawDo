// End-to-end mirror test: two real stores (client + "server") talking through
// the actual wire protocol over an in-process transport. Verifies the whole
// Track-A stack: negentropy reconciliation, mirror ops, deletes propagating,
// server-side drift healing, the empty-wipe guard, and down-restore.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "../db/store.h"
#include "mirror.h"
#include "protocol.h"
#include "server.h"

#define REQUIRE(cond, msg) do { \
    if (!(cond)) { printf("SYNC FAIL: %s\n", msg); return 1; } \
} while (0)

static store::Id vid(uint64_t ts, uint64_t n) {  // version id from a test uuid
    uint8_t uuid[24];
    for (int i = 0; i < 24; i++) uuid[i] = (uint8_t)((n * 61 + i * 13) & 0xFF);
    return jsync::make_version_id(ts, uuid);
}

static std::map<std::string, std::string> dump(store::Store* s) {
    std::map<std::string, std::string> m;
    store::load_all(s, [&](const store::Item& it) {
        m[std::string((const char*)it.id.data(), store::ID_SIZE)] = it.payload;
    });
    return m;
}

int sync_test_main(const char* base_dir) {
    namespace fs = std::filesystem;
    std::string base = base_dir;
    fs::remove_all(base);

    auto* client = store::open(base + "/client");
    auto* server = store::open(base + "/server");
    jsync::Transport transport = [&](std::string_view req) {
        return jsync::sync_handle(server, req);
    };

    // initial state: 40 items on the client, empty server
    store::write(client, [](store::Writer& w) {
        for (uint64_t n = 0; n < 40; n++) {
            w.put(1000 + n, vid(1000 + n, n), "todo #" + std::to_string(n));
        }
    });
    auto st = jsync::mirror_up(client, transport);
    REQUIRE(st.converged, "initial mirror converged");
    REQUIRE(st.pushed == 40 && st.deleted_remote == 0, "initial mirror pushed all");

    // no-change mirror should move nothing
    st = jsync::mirror_up(client, transport);
    REQUIRE(st.converged && st.pushed == 0 && st.deleted_remote == 0, "no-op mirror");

    // local mutations: edit 5 (delete old version, put new version), delete 3, add 7
    store::write(client, [](store::Writer& w) {
        for (uint64_t n = 0; n < 5; n++) {
            w.del(1000 + n, vid(1000 + n, n));
            w.put(5000 + n, vid(5000 + n, n), "edited #" + std::to_string(n));
        }
        for (uint64_t n = 10; n < 13; n++) w.del(1000 + n, vid(1000 + n, n));
        for (uint64_t n = 100; n < 107; n++) {
            w.put(6000 + n, vid(6000 + n, n), "new #" + std::to_string(n));
        }
    });
    st = jsync::mirror_up(client, transport);
    REQUIRE(st.converged, "mutation mirror converged");
    REQUIRE(st.pushed == 12, "pushed 5 edits + 7 adds");         // new versions
    REQUIRE(st.deleted_remote == 8, "deleted 5 old versions + 3"); // stale keys
    REQUIRE(dump(client) == dump(server), "byte-identical after mutations");

    // inject server-side drift (bug, cosmic ray, meddling): extra + missing
    store::write(server, [](store::Writer& w) {
        w.put(7777, vid(7777, 999), "ghost item");
        w.del(1020, vid(1020, 20));
    });
    st = jsync::mirror_up(client, transport);
    REQUIRE(st.converged, "drift healed");
    REQUIRE(st.pushed == 1 && st.deleted_remote == 1, "drift ops exact");
    REQUIRE(dump(client) == dump(server), "byte-identical after healing");

    // direction-confusion guard: empty local store must refuse to wipe server
    auto* fresh = store::open(base + "/fresh");
    bool threw = false;
    try {
        jsync::mirror_up(fresh, transport);
    } catch (const std::runtime_error&) { threw = true; }
    REQUIRE(threw, "empty-wipe guard threw");
    REQUIRE(store::item_count(server) > 0, "server untouched by refused mirror");

    // down-restore: the fresh store pulls everything (fresh-install path)
    st = jsync::mirror_down(fresh, transport);
    REQUIRE(st.converged, "restore converged");
    REQUIRE(st.fetched == store::item_count(server), "restore fetched all");
    REQUIRE(dump(fresh) == dump(client), "restored store equals master");

    // restore is also a mirror: local-only junk gets removed
    store::write(fresh, [](store::Writer& w) { w.put(8888, vid(8888, 998), "junk"); });
    st = jsync::mirror_down(fresh, transport);
    REQUIRE(st.converged && st.deleted_local == 1, "restore removed local junk");

    store::close(fresh);
    store::close(client);
    store::close(server);
    printf("OK: sync test passed\n");
    return 0;
}
