#include "check.h"
#include "main_data_containers/hash_table.h"
#include "main_data_containers/endless_virtual_memory_field.h"
#include <map>
#include <random>
#include <vector>

namespace table_tests {
namespace table = hash_table;
using model_key = std::pair<uint64_t, uint64_t>;
using model = std::map<model_key, table::value>;

model_key pair(table::key k) { return {k.high64, k.low64}; }
bool same(table::value a, table::value b) {
  return a.root == b.root && a.node_allocator == b.node_allocator &&
         a.hash_accumulator_array == b.hash_accumulator_array;
}

void verify(table::two_way *t, const model &expected) {
  CHECK(table::count(t) == expected.size());
  size_t occupied = t->has_empty_key ? 1 : 0;
  model found;
  if (t->has_empty_key) found.emplace(pair(table::EMPTY), t->empty_key_value);
  for (size_t i = 0; i < t->bucket_count; ++i) {
    bool ended = false;
    for (size_t j = 0; j < table::BUCKET_SIZE; ++j) {
      table::key k = t->data[i].keys[j];
      if (table::operator==(k, table::EMPTY)) { ended = true; continue; }
      CHECK(!ended);
      CHECK((k.high64 & (t->bucket_count - 1)) == i || (k.low64 & (t->bucket_count - 1)) == i);
      CHECK(found.emplace(pair(k), t->data[i].values[j]).second);
      ++occupied;
    }
  }
  CHECK(occupied == expected.size());
  CHECK(found.size() == expected.size());
  for (auto [k, v] : expected) {
    table::key key{.low64 = k.second, .high64 = k.first};
    auto *got = table::get(t, key);
    CHECK(got != nullptr && same(*got, v));
    CHECK(table::contains(t, key));
    CHECK(same(table::unsafe_get(t, key), v));
    CHECK(found.contains(k) && same(found.at(k), v));
  }
}

inline size_t allocation_calls = 0;
inline size_t fail_at = 0;
void *test_allocate(size_t alignment, size_t bytes) {
  ++allocation_calls;
  if (allocation_calls == fail_at) return nullptr;
  return std::aligned_alloc(alignment, bytes);
}

void presence_and_boundaries() {
  static_assert(sizeof(table::bucket) == 192);
  static_assert(alignof(table::bucket) == 64);
  auto t = table::create(1, 16);
  CHECK(!table::bad(&t));
  CHECK(reinterpret_cast<uintptr_t>(t.data) % 64 == 0);
  CHECK(table::get(&t, {}) == nullptr);
  CHECK(table::get(&t, table::EMPTY) == nullptr);
  model expected;
  const table::key keys[] = {{0, 0}, {0, UINT64_MAX}, {UINT64_MAX, 0}, table::EMPTY};
  for (auto key : keys) {
    CHECK(table::associate(&t, key, {}));
    expected.emplace(pair(key), table::value{});
    CHECK(augmented_tree::is_empty(table::get(&t, key)));
    verify(&t, expected);
    // Mutate the stored header, rather than a copy returned from lookup.
    table::get(&t, key)->root = 42;
    expected[pair(key)].root = 42;
    verify(&t, expected);
    CHECK(table::associate(&t, key, {}));
    expected[pair(key)] = {};
    verify(&t, expected);
  }
  CHECK(table::grow(&t));
  verify(&t, expected);
  for (auto key : keys) {
    CHECK(table::dissociate(&t, key));
    CHECK(!table::dissociate(&t, key));
    expected.erase(pair(key));
    verify(&t, expected);
  }
  CHECK(table::unsafe_insert(&t, table::EMPTY, {}));
  table::clear(&t);
  CHECK(table::get(&t, table::EMPTY) == nullptr);
  verify(&t, {});
  table::destroy(&t);
  CHECK(table::bad(&t));
  CHECK(table::get(&t, {}) == nullptr);
  CHECK(!table::associate(&t, {}, {}));
  CHECK(!table::dissociate(&t, {}));
  CHECK(!table::grow(&t));
  table::clear(&t);
  table::destroy(&t);
}

void collisions_and_deletion() {
  auto t = table::create(2, 2);
  model expected;
  std::vector<table::key> keys;
  // All keys have the same two choices, filling both buckets completely.
  for (uint64_t i = 0; i < 2 * table::BUCKET_SIZE; ++i) {
    table::key k{.low64 = i * 2 + 1, .high64 = i * 2};
    keys.push_back(k);
    table::value v{};
    v.root = uint32_t(i);
    CHECK(table::associate(&t, k, v));
    expected.emplace(pair(k), v);
  }
  verify(&t, expected);
  CHECK(!table::associate(&t, {101, 100}, {}));
  verify(&t, expected);
  for (size_t i : {size_t(0), table::BUCKET_SIZE - 1, 2 * table::BUCKET_SIZE - 1,
                   size_t(1), table::BUCKET_SIZE}) {
    table::unsafe_dissociate(&t, keys[i]);
    expected.erase(pair(keys[i]));
    verify(&t, expected);
  }
  CHECK(table::associate(&t, {101, 100}, {}));
  expected.emplace(pair({101, 100}), table::value{});
  verify(&t, expected);
  table::clear(&t);
  verify(&t, {});
  table::destroy(&t);

  // Same-bucket choices that remain clustered across every allowed size.
  t = table::create(1, 32);
  expected.clear();
  for (uint64_t i = 0; i < table::BUCKET_SIZE; ++i) {
    table::key k{256 * i, 256 * (i + 1)};
    CHECK(table::associate(&t, k, {}));
    expected.emplace(pair(k), table::value{});
  }
  table::bucket *original = t.data;
  auto *original_value = table::get(&t, {0, 256});
  CHECK(!table::associate(&t, {2048, 2304}, {}));
  CHECK(t.data == original && t.bucket_count == 1);
  CHECK(table::get(&t, {0, 256}) == original_value);
  verify(&t, expected);
  table::destroy(&t);
}

void allocation_failures() {
  for (size_t invalid : {size_t(0), size_t(3), SIZE_MAX}) {
    auto t = table::create(invalid);
    CHECK(table::bad(&t));
  }
  auto invalid = table::create(16, 8);
  CHECK(table::bad(&invalid));
  invalid = table::create(1, SIZE_MAX);
  CHECK(table::bad(&invalid));
  allocation_calls = 0; fail_at = 1;
  auto t = table::create(1, 16, test_allocate);
  CHECK(table::bad(&t) && allocation_calls == 1);

  t = table::create(1, 16);
  model expected;
  for (uint64_t i = 0; i < table::BUCKET_SIZE; ++i) {
    table::key k{i, i};
    CHECK(table::associate(&t, k, {}));
    expected.emplace(pair(k), table::value{});
  }
  table::bucket *original = t.data;
  allocation_calls = 0; fail_at = 1;
  const table::key extra{table::BUCKET_SIZE, table::BUCKET_SIZE};
  CHECK(!table::associate(&t, extra, {}, test_allocate));
  CHECK(t.data == original && t.bucket_count == 1 && allocation_calls == 1);
  verify(&t, expected);
  allocation_calls = 0;
  CHECK(!table::grow(&t, test_allocate));
  CHECK(t.data == original && t.bucket_count == 1);
  // Existing entries, including the special key, need no allocation to update.
  allocation_calls = 0;
  CHECK(table::associate(&t, {0, 0}, {}, test_allocate));
  CHECK(allocation_calls == 0);
  CHECK(table::associate(&t, table::EMPTY, {}, test_allocate));
  expected.emplace(pair(table::EMPTY), table::value{});
  CHECK(allocation_calls == 0);
  CHECK(table::associate(&t, extra, {}));
  expected.emplace(pair(extra), table::value{});
  CHECK(t.bucket_count == 2);
  verify(&t, expected);
  table::destroy(&t);

  t = table::create(1, 16);
  expected.clear();
  for (uint64_t i = 0; i < table::BUCKET_SIZE; ++i) {
    table::key k{256 * i, 256 * (i + 1)};
    CHECK(table::associate(&t, k, {}));
    expected.emplace(pair(k), table::value{});
  }
  original = t.data;
  allocation_calls = 0; fail_at = 2;
  CHECK(!table::associate(&t, {2048, 2304}, {}, test_allocate));
  CHECK(allocation_calls == 2 && t.data == original && t.bucket_count == 1);
  verify(&t, expected);
  table::destroy(&t);
}

void random_operations() {
  auto t = table::create(1, 4096);
  std::mt19937_64 random(0xBEA4);
  std::vector<table::key> keys{table::EMPTY, {0, 0}};
  while (keys.size() < 4096) keys.push_back({random(), random()});
  model expected;
  for (size_t step = 0; step < 50000; ++step) {
    auto key = keys[random() % keys.size()];
    switch (random() % 4) {
      case 0: CHECK(table::dissociate(&t, key) == (expected.erase(pair(key)) != 0)); break;
      case 1:
      case 2: {
        table::value v{};
        v.root = random() % 4 == 0 ? UINT32_MAX : uint32_t(random());
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
  CHECK(t.bucket_count > 1);
  verify(&t, expected);
  table::destroy(&t);
}

void actual_namespaces() {
  namespace arena = geometric_rank_partioned_virtual_slab_arena;
  auto [nodes, error] = arena::create();
  CHECK(!error);
  auto *hashes = endless_virtual_memory_field::create();
  CHECK(hashes != nullptr);
  auto t = table::create(1, 16);
  const table::key a{1, 2}, b{3, 4};
  CHECK(table::associate(&t, a, augmented_tree::create(&nodes, hashes)));
  CHECK(table::associate(&t, b, augmented_tree::create(&nodes, hashes)));
  CHECK(augmented_tree::unsafe_associate(table::get(&t, a), 10, 11, UINT64_MAX) == 0);
  CHECK(augmented_tree::unsafe_associate(table::get(&t, b), 20, 22, UINT64_MAX) == 0);
  CHECK(table::grow(&t));
  CHECK(augmented_tree::get(table::get(&t, a), 10) == 11);
  CHECK(augmented_tree::get(table::get(&t, a), 20) == 0);
  CHECK(augmented_tree::get(table::get(&t, b), 20) == 22);
  augmented_tree::unsafe_dissociate(table::get(&t, a), 10, UINT64_MAX);
  CHECK(table::contains(&t, a));
  CHECK(augmented_tree::is_empty(table::get(&t, a)));
  CHECK(table::dissociate(&t, a));
  CHECK(table::get(&t, a) == nullptr);
  augmented_tree::unsafe_dissociate(table::get(&t, b), 20, UINT64_MAX);
  table::clear(&t);
  CHECK(table::get(&t, b) == nullptr);
  table::destroy(&t);
  endless_virtual_memory_field::destroy(hashes);
  arena::destroy(&nodes);
}
}

int main() {
  table_tests::presence_and_boundaries();
  table_tests::collisions_and_deletion();
  table_tests::allocation_failures();
  table_tests::random_operations();
  table_tests::actual_namespaces();
  std::puts("PASS: namespace presence, full key space, collisions, growth/failure, 50,000 random operations, and live trees");
}
