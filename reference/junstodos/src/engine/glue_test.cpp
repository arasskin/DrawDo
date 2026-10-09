// Glue tests: entity conventions, the :updated-at bump, load/sync_to_store
// roundtrips, deletion via full retraction, and the closed loop healing
// store-side drift.

#include <cstdio>
#include <filesystem>
#include <string>

#include "glue.h"
#include "../sync/protocol.h"

using edn::M;
using edn::Value;
using engine::Db;
using engine::Diff;

#define REQUIRE(cond, msg) do { \
    if (!(cond)) { printf("GLUE FAIL: %s\n", msg); return 1; } \
} while (0)

static Value kw(const char* s) { return Value::keyword(s); }

static Db add_todo(Db db, const Value& id, const char* text, bool done) {
    Diff d;
    d.adds = {{id, kw("text"), Value::string(text)},
              {id, kw("done"), Value::boolean(done)}};
    return glue::transact(std::move(db), d);
}

int glue_test_main(const char* dir) {
    std::filesystem::remove_all(dir);

    Db db;
    Value a = glue::new_entity_id();
    Value b = glue::new_entity_id();
    REQUIRE(a != b, "entity ids unique");

    // pattern-form transact: same literal shape query reads, bump included
    {
        Db pdb;
        Value c = glue::new_entity_id();
        pdb = glue::transact(std::move(pdb),
                             M{{c, M{{kw("text"), "pattern form"}, {kw("done"), false}}}});
        REQUIRE(engine::query(pdb, M{{c, M{{kw("text"), edn::sym("?t")}}}})
                        .get(c).get(kw("text")) == Value("pattern form"),
                "pattern transact stores attrs");
        REQUIRE(glue::entity_updated_at(pdb, c) > 0, "pattern transact bumps updated-at");
        bool threw = false;
        try { glue::transact(pdb, "{?e {:done true}}"); }
        catch (const std::runtime_error&) { threw = true; }
        REQUIRE(threw, "non-ground transact pattern throws");
    }

    db = add_todo(db, a, "water the plants", false);
    db = add_todo(db, b, "port the engine", true);

    uint64_t ts_a = glue::entity_updated_at(db, a);
    REQUIRE(ts_a > 0, "transact bumped :updated-at");

    // monotonic bump on edit
    db = add_todo(db, a, "water the plants twice", false);
    REQUIRE(glue::entity_updated_at(db, a) > ts_a, "edit bumps updated-at");

    // memory -> storage
    auto* s = store::open(dir);
    REQUIRE(glue::sync_to_store(db, s), "first sync writes");
    REQUIRE(store::item_count(s) == 2, "two entities stored");
    REQUIRE(!glue::sync_to_store(db, s), "no-op sync writes nothing");

    // storage -> memory roundtrip preserves everything queryable
    Db loaded = glue::load(s);
    REQUIRE(engine::query(loaded, edn::read_string("{?e {:done true}}")) ==
                engine::query(db, edn::read_string("{?e {:done true}}")),
            "roundtrip preserves query results");
    REQUIRE(glue::entity_updated_at(loaded, a) == glue::entity_updated_at(db, a),
            "roundtrip preserves updated-at");

    // deletion: retract everything -> gone from store on next sync
    db = glue::retract_entity(db, b);
    REQUIRE(glue::sync_to_store(db, s), "delete syncs");
    REQUIRE(store::item_count(s) == 1, "deleted entity left the store");

    // closed loop: corrupt the store directly; sync heals it
    store::write(s, [&](store::Writer& w) {
        uint8_t junk_uuid[24] = {9, 9, 9};
        w.put(12345, jsync::make_version_id(12345, junk_uuid), "{:ghost true}");
    });
    REQUIRE(store::item_count(s) == 2, "drift injected");
    REQUIRE(glue::sync_to_store(db, s), "heal sync writes");
    REQUIRE(store::item_count(s) == 1, "drift healed");
    REQUIRE(!glue::sync_to_store(db, s), "stable after heal");

    store::close(s);
    printf("OK: glue test passed\n");
    return 0;
}
