#include "check.h"
#include "namespaces.h"
#include <map>
#include <random>
#include <vector>

namespace table_tests {
namespace table = hash_table;
using model_key = std::pair<uint64_t, uint64_t>;
using model = std::map<model_key, table::value>;
model_key pair(table::key k) { return {k.high64, k.low64}; }
bool same(table::value a, table::value b) {
  return a.root == b.root && a.node_allocator == b.node_allocator && a.rank == b.rank;
}
void verify(table::directory *t, const model& expected) {
  CHECK(table::count(t) == expected.size());
  model found;
  table::for_each(t, [&](table::key k, table::value v) { CHECK(found.emplace(pair(k), v).second); });
  CHECK(found.size() == expected.size());
  for (auto [k, v] : expected) {
    table::key key{.low64 = k.second, .high64 = k.first};
    auto *got = table::get(t, key);
    CHECK(got && same(*got, v));
    CHECK(table::contains(t, key));
    CHECK(same(table::unsafe_get(t, key), v));
    CHECK(found.contains(k) && same(found.at(k), v));
  }
}
inline size_t allocation_calls = 0, fail_at = 0;
void *test_allocate(size_t alignment, size_t bytes) {
  if (++allocation_calls == fail_at) return nullptr;
  return std::aligned_alloc(alignment, bytes);
}
void presence_and_boundaries() {
  static_assert(sizeof(table::value) == 24);
  static_assert(!std::is_copy_constructible_v<table::directory>);
  auto t = table::create(0, test_allocate);
  CHECK(!table::bad(&t) && allocation_calls == 0);
  CHECK(table::memory_usage(&t) == sizeof(t));
  model expected;
  const table::key keys[] = {{0, 0}, {0, UINT64_MAX}, {UINT64_MAX, 0}, table::ALL_ONES};
  for (auto key : keys) {
    CHECK(!table::contains(&t, key));
    CHECK(table::associate(&t, key, {}));
    CHECK(record_index::is_empty(table::get(&t, key)));
    table::get(&t, key)->rank = 42;
    expected[pair(key)].rank = 42;
    verify(&t, expected);
  }
  // Existing values can change without allocation.
  allocation_calls = 0; fail_at = 1;
  CHECK(table::associate(&t, keys[0], {}));
  expected[pair(keys[0])] = {};
  CHECK(allocation_calls == 0);
  verify(&t, expected);
  for (auto key : keys) {
    CHECK(table::dissociate(&t, key));
    CHECK(!table::dissociate(&t, key));
    expected.erase(pair(key));
    verify(&t, expected);
  }
  CHECK(allocation_calls == 0);
  fail_at = 0;
  CHECK(table::unsafe_insert(&t, table::ALL_ONES, {}));
  table::clear(&t);
  verify(&t, {});
  table::destroy(&t);
  CHECK(table::bad(&t));
  CHECK(!table::associate(&t, {}, {}));
  CHECK(!table::reserve(&t, 1));
  CHECK(!table::dissociate(&t, {}));
  table::clear(&t);
  table::destroy(&t);
}
void collisions_and_deletion() {
  auto t = table::create();
  model expected;
  // All share the exact hash. Equality must still use high64, without an early
  // capacity failure from the former table's two-choice collisions.
  for (uint64_t i = 0; i < 512; ++i) {
    table::key k{123, i};
    table::value v{}; v.rank = unsigned(i + 1);
    CHECK(table::unsafe_insert(&t, k, v));
    expected.emplace(pair(k), v);
  }
  verify(&t, expected);
  for (uint64_t i = 0; i < 512; i += 2) {
    CHECK(table::dissociate(&t, {123, i}));
    expected.erase(pair({123, i}));
  }
  verify(&t, expected);
  CHECK(!table::get(&t, {123, 999}));
  table::destroy(&t);
}
void allocation_failures() {
  auto invalid = table::create(SIZE_MAX);
  CHECK(table::bad(&invalid));
  invalid = table::create(0, nullptr);
  CHECK(table::bad(&invalid));
  // Fail every allocation in eager creation, then every allocation while
  // lazily building a directory through multiple value and index growths.
  size_t creation_failures = 0, insert_failures = 0, populated_failures = 0;
  for (size_t failure = 1; ; ++failure) {
    allocation_calls = 0; fail_at = failure;
    auto t = table::create(1024, test_allocate);
    if (!table::bad(&t)) break;
    ++creation_failures;
  }
  CHECK(creation_failures >= 2);
  for (size_t failure = 1; ; ++failure) {
    allocation_calls = 0; fail_at = failure;
    auto t = table::create(0, test_allocate);
    model expected;
    bool failed = false;
    std::mt19937_64 random(123);
    for (size_t i = 0; i < 1024; ++i) {
      table::key k{random(), random()};
      table::value v{}; v.rank = unsigned(i + 1);
      if (!table::unsafe_insert(&t, k, v)) {
        failed = true; ++insert_failures;
        populated_failures += !expected.empty();
        verify(&t, expected);
        CHECK(!table::contains(&t, k));
        fail_at = 0;
        CHECK(table::unsafe_insert(&t, k, v));
      }
      expected.emplace(pair(k), v);
    }
    verify(&t, expected);
    table::destroy(&t);
    if (!failed) break;
  }
  CHECK(insert_failures > 10 && populated_failures > 5);
  // reserve can relocate values before the index allocation fails. Contents
  // must remain searchable; old value addresses are intentionally not retained.
  size_t reserve_failures = 0;
  for (size_t failure = 1; ; ++failure) {
    fail_at = 0;
    auto t = table::create(0, test_allocate);
    model expected;
    for (uint64_t i = 0; i < 16; ++i) {
      CHECK(table::associate(&t, {i, i}, {}));
      expected.emplace(pair({i,i}), table::value{});
    }
    allocation_calls = 0; fail_at = failure;
    bool ok = table::reserve(&t, 4096);
    verify(&t, expected);
    fail_at = 0;
    CHECK(table::reserve(&t, 4096));
    verify(&t, expected);
    table::destroy(&t);
    if (ok) break;
    ++reserve_failures;
  }
  CHECK(reserve_failures >= 2);
  std::printf("Dense allocation failures checked: create=%zu insert=%zu reserve=%zu\n",
              creation_failures, insert_failures, reserve_failures);
}
void random_operations() {
  auto t = table::create(1);
  std::mt19937_64 random(0xBEA4);
  std::vector<table::key> keys{table::ALL_ONES, {0, 0}};
  while (keys.size() < 4096) keys.push_back({random(), random()});
  model expected;
  for (size_t step = 0; step < 50000; ++step) {
    auto key = keys[random() % keys.size()];
    switch (random() % 4) {
      case 0: CHECK(table::dissociate(&t, key) == (expected.erase(pair(key)) != 0)); break;
      case 1:
      case 2: {
        table::value v{};
        v.root = reinterpret_cast<void*>(uintptr_t(random()));
        CHECK(table::associate(&t, key, v));
        expected[pair(key)] = v;
        break;
      }
      default: break;
    }
    auto reference = expected.find(pair(key));
    auto *found = table::get(&t, key);
    CHECK((found != nullptr) == (reference != expected.end()));
    if (found != nullptr) CHECK(same(*found, reference->second));
    CHECK(table::count(&t) == expected.size());
    if (step % 997 == 0) verify(&t, expected);
  }
  CHECK(t.entries.bucket_count() > 1);
  verify(&t, expected);
  table::destroy(&t);
}

void grows_past_former_limit() {
  auto t = table::create();
  constexpr size_t total = 262145;
  std::mt19937_64 random(876);
  std::vector<table::key> keys;
  for (size_t i = 0; i < total; ++i) {
    keys.push_back({random(), random()});
    table::value v{}; v.rank = unsigned(i + 1);
    CHECK(table::unsafe_insert(&t, keys.back(), v));
  }
  CHECK(table::count(&t) == total);
  for (size_t i = 0; i < total; ++i) CHECK(table::get(&t, keys[i])->rank == i + 1);
  table::destroy(&t);
}
void actual_namespaces() {
  record_index::pool nodes;
  auto t = table::create(1);
  const table::key a{1, 2}, b{3, 4};
  CHECK(table::associate(&t, a, record_index::create(&nodes)));
  CHECK(table::associate(&t, b, record_index::create(&nodes)));
  CHECK(record_index::unsafe_associate(table::get(&t, a), 10, 11, UINT64_MAX) == 0);
  CHECK(record_index::unsafe_associate(table::get(&t, b), 20, 22, UINT64_MAX) == 0);
  CHECK(table::reserve(&t, 16));
  CHECK(record_index::get(table::get(&t, a), 10) == 11);
  CHECK(record_index::get(table::get(&t, a), 20) == 0);
  CHECK(record_index::get(table::get(&t, b), 20) == 22);
  CHECK(record_index::erase(table::get(&t, a),10)==record_index::erase_result::removed);
  CHECK(table::contains(&t, a));
  CHECK(record_index::is_empty(table::get(&t, a)));
  CHECK(table::dissociate(&t, a));
  CHECK(table::get(&t, a) == nullptr);
  CHECK(record_index::erase(table::get(&t, b),20)==record_index::erase_result::removed);
  table::clear(&t);
  CHECK(table::get(&t, b) == nullptr);
  table::destroy(&t);
  CHECK(nodes.stats().live_bytes==0);
  nodes.trim();
  CHECK(nodes.stats().reserved_bytes==0);
}}
int main() {
  table_tests::presence_and_boundaries();
  table_tests::collisions_and_deletion();
  table_tests::allocation_failures();
  table_tests::random_operations();
  table_tests::grows_past_former_limit();
  table_tests::actual_namespaces();
  std::puts("PASS: dense namespace presence, full keys, collisions, growth/failure, 50,000 random operations, and live trees");
}
