// Store module test (single-dbi keyed-set design). The store resolves
// nothing; this verifies it faithfully executes mirror ops — including the
// del(old)+put(new) pair a merge-walk emits for an edit — and that iteration
// order, persistence, and txn rollback behave.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "store.h"

static store::Id test_id(uint64_t n) {
    store::Id id{};
    for (size_t i = 0; i < id.size(); i++) id[i] = (uint8_t)((n * 37 + i * 11) & 0xFF);
    return id;
}

#define REQUIRE(cond, msg) do { \
    if (!(cond)) { printf("STORE FAIL: %s\n", msg); return 1; } \
} while (0)

static std::vector<store::Item> dump(store::Store* s) {
    std::vector<store::Item> v;
    store::load_all(s, [&](const store::Item& it) { v.push_back(it); });
    return v;
}

int store_test_main(const char* dir) {
    std::filesystem::remove_all(dir);

    auto* s = store::open(dir);

    // mirror batch: three inserts in one txn
    store::write(s, [](store::Writer& w) {
        w.put(100, test_id(1), "todo: buy milk");
        w.put(300, test_id(3), "todo: ship it");
        w.put(200, test_id(2), "todo: port datalog engine");
    });
    REQUIRE(store::item_count(s) == 3, "count after insert");

    // iteration must come back in (updated_at, id) key order
    auto items = dump(s);
    REQUIRE(items.size() == 3, "load_all size");
    REQUIRE(items[0].updated_at == 100 && items[1].updated_at == 200 &&
            items[2].updated_at == 300, "key order");

    // edit, as the merge-walk emits it: del(old key) + put(new key)
    store::write(s, [](store::Writer& w) {
        w.del(100, test_id(1));
        w.put(500, test_id(1), "todo: buy oat milk");
    });
    REQUIRE(store::item_count(s) == 3, "count after edit");
    items = dump(s);
    REQUIRE(items[2].updated_at == 500 && items[2].payload == "todo: buy oat milk",
            "edited item at new key");

    // delete + idempotent re-delete / absent delete
    store::write(s, [](store::Writer& w) {
        w.del(200, test_id(2));
        w.del(200, test_id(2));   // repeat: no-op
        w.del(999, test_id(42));  // absent: no-op
    });
    REQUIRE(store::item_count(s) == 2, "count after delete");

    // duplicate id at two timestamps is representable (interrupted-mirror
    // state); the store must keep both faithfully
    store::write(s, [](store::Writer& w) {
        w.put(600, test_id(3), "todo: ship it v2");  // old (300, id3) still present
    });
    REQUIRE(store::item_count(s) == 3, "count with duplicate id");
    store::write(s, [](store::Writer& w) {
        w.del(300, test_id(3));  // the healing op a later walk would emit
    });
    REQUIRE(store::item_count(s) == 2, "count after heal");

    // persistence across close/reopen (exercises idempotent schema setup)
    store::close(s);
    s = store::open(dir);
    REQUIRE(store::item_count(s) == 2, "count after reopen");
    items = dump(s);
    REQUIRE(items[0].updated_at == 500 && items[0].payload == "todo: buy oat milk",
            "item 1 after reopen");
    REQUIRE(items[1].updated_at == 600 && items[1].payload == "todo: ship it v2",
            "item 3 after reopen");
    REQUIRE(memcmp(items[0].id.data(), test_id(1).data(), store::ID_SIZE) == 0,
            "item 1 id roundtrip");

    // abort path: a throwing write must leave no trace
    try {
        store::write(s, [](store::Writer& w) {
            w.put(700, test_id(7), "never lands");
            throw std::runtime_error("deliberate abort");
        });
        printf("STORE FAIL: write did not rethrow\n");
        return 1;
    } catch (const std::runtime_error&) {}
    REQUIRE(store::item_count(s) == 2, "count after aborted txn");

    store::close(s);
    printf("OK: store test passed\n");
    return 0;
}
