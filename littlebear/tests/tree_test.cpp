#include "check.h"
#include "main_data_containers/augmented_tree.h"
#include "main_data_containers/endless_virtual_memory_field.h"
#include <map>
#include <random>
#include <set>
#include <vector>

namespace tree_tests {
namespace tree = augmented_tree;
namespace arena = geometric_rank_partioned_virtual_slab_arena;

struct record { uint64_t value; uint64_t hash; };
using model = std::map<uint64_t, record>;
struct fixture { arena::allocator nodes; uint64_t *hashes; tree::header index; };

void create(fixture *f) {
  auto result = arena::create();
  CHECK(!result.has_error);
  f->nodes = result.a;
  f->hashes = endless_virtual_memory_field::create();
  CHECK(f->hashes != nullptr);
  f->index = tree::create(&f->nodes, f->hashes);
}

void destroy(fixture *f) {
  arena::destroy(&f->nodes);
  endless_virtual_memory_field::destroy(f->hashes);
}

struct summary { uint64_t low; uint64_t high; uint64_t hash; size_t leaves; };

summary inspect(fixture *f, uint32_t index, const model &expected, std::set<uint32_t> *seen) {
  CHECK(index != tree::INVALID_NODE_INDEX);
  CHECK(seen->insert(index).second);
  auto node = tree::node_get_data(&f->index, index);
  if (tree::node_rank(index) == -1) {
    const uint64_t key = node[tree::node_item::k].value;
    auto item = expected.find(key);
    CHECK(item != expected.end());
    CHECK(node[tree::node_item::v].value == item->second.value);
    CHECK(f->hashes[index] == item->second.hash);
    return {key, key, item->second.hash, 1};
  }
  uint32_t left = node[tree::node_item::children].left_index;
  uint32_t right = node[tree::node_item::children].right_index;
  if (tree::node_rank(left) != -1) CHECK(tree::compare_node_ranks(index, left) > 0);
  if (tree::node_rank(right) != -1) CHECK(tree::compare_node_ranks(index, right) > 0);
  auto l = inspect(f, left, expected, seen);
  auto r = inspect(f, right, expected, seen);
  CHECK(l.high < r.low);
  CHECK(node[tree::node_item::pathmarker].value == r.low);
  if (f->hashes[index] != (l.hash ^ r.hash)) {
    std::fprintf(stderr, "bad fingerprint node=%u pivot=%llu left=[%llu,%llu] right=[%llu,%llu]\n",
      index, (unsigned long long)r.low, (unsigned long long)l.low, (unsigned long long)l.high,
      (unsigned long long)r.low, (unsigned long long)r.high);
    for (auto [key, value] : expected)
      std::fprintf(stderr, "  key=%llu hash=%llu\n", (unsigned long long)key, (unsigned long long)value.hash);
  }
  CHECK(f->hashes[index] == (l.hash ^ r.hash));
  return {l.low, r.high, l.hash ^ r.hash, l.leaves + r.leaves};
}

size_t allocated_nodes(fixture *f) {
  size_t count = 0;
  std::set<uint32_t> free;
  for (uint8_t rank = 0; rank < arena::NUMBER_OF_RANKS; ++rank) {
    count += f->nodes.by_rank[rank].high_watermark - arena::partition_begin(rank);
    for (uint32_t index = f->nodes.by_rank[rank].head.next_index;
         index != arena::INVALID_INDEX;) {
      CHECK(free.insert(index).second);
      CHECK(arena::partition(index) == rank);
      CHECK(index < f->nodes.by_rank[rank].high_watermark);
      CHECK(count > 0);
      --count;
      index = static_cast<arena::slab *>(arena::address(&f->nodes, index))->next_index;
    }
  }
  return count;
}

void range(fixture *f, const model &expected, uint64_t low, uint64_t high) {
  uint64_t hash = 0;
  for (auto it = expected.lower_bound(low); it != expected.end() && it->first < high; ++it)
    hash ^= it->second.hash;
  CHECK(tree::get_range_fingerprint(&f->index, low, high) == hash);
}

void verify(fixture *f, const model &expected) {
  CHECK(tree::is_empty(&f->index) == expected.empty());
  if (!expected.empty()) {
    std::set<uint32_t> seen;
    auto total = inspect(f, f->index.root, expected, &seen);
    CHECK(total.leaves == expected.size());
    CHECK(tree::get_full_fingerprint(&f->index) == total.hash);
    CHECK(seen.size() == expected.size() * 2 - 1);
    CHECK(allocated_nodes(f) == seen.size());
    for (auto [key, record] : expected) CHECK(tree::get(&f->index, key) == record.value);
  } else {
    CHECK(allocated_nodes(f) == 0);
    CHECK(tree::get_full_fingerprint(&f->index) == 0);
  }
  range(f, expected, 0, UINT64_MAX);
  range(f, expected, 0, 0);
  range(f, expected, UINT64_MAX, UINT64_MAX);
  range(f, expected, 100, 0);
}

void insert(fixture *f, model *expected, uint64_t key, uint64_t value, uint64_t hash) {
  std::snprintf(testing::context, sizeof(testing::context), "insert key=%llu hash=%llu size=%zu",
                (unsigned long long)key, (unsigned long long)hash, expected->size());
  CHECK(tree::unsafe_associate(&f->index, key, value, hash) == tree::NO_ERRORS);
  CHECK(expected->emplace(key, record{value, hash}).second);
  verify(f, *expected);
}

void erase(fixture *f, model *expected, uint64_t key) {
  std::snprintf(testing::context, sizeof(testing::context), "erase key=%llu size=%zu",
                (unsigned long long)key, expected->size());
  tree::unsafe_dissociate(&f->index, key, expected->at(key).hash);
  CHECK(expected->erase(key) == 1);
  verify(f, *expected);
}

void sequences() {
  for (unsigned order = 0; order < 4; ++order) {
    fixture f;
    create(&f);
    model expected;
    std::mt19937_64 random(17 + order);
    std::vector<uint64_t> keys;
    for (uint64_t i = 0; i < 128; ++i) keys.push_back(i);
    if (order == 1) std::reverse(keys.begin(), keys.end());
    if (order >= 2) std::shuffle(keys.begin(), keys.end(), random);
    for (auto key : keys) {
      // Equal ranks stress index tie-breaking and long spines.
      const uint64_t hash = order == 3 ? UINT64_MAX : random();
      insert(&f, &expected, key, key + 1, hash);
    }
    if (order >= 2) std::shuffle(keys.begin(), keys.end(), random);
    for (auto key : keys) erase(&f, &expected, key);
    destroy(&f);
  }
}

void random_operations() {
  for (unsigned seed = 0; seed < 8; ++seed) {
    fixture f;
    create(&f);
    model expected;
    std::mt19937_64 random(0xD0D0 + seed);
    for (unsigned step = 0; step < 5000; ++step) {
      const uint64_t key = random() % 128;
      auto existing = expected.find(key);
      if (random() % 3 == 0) {
        if (existing != expected.end()) erase(&f, &expected, key);
      } else if (existing != expected.end()) {
        const record replacement{random() | 1, random()};
        CHECK(tree::replace(&f.index, key, replacement.value, replacement.hash));
        existing->second = replacement;
        verify(&f, expected);
      } else insert(&f, &expected, key, random() | 1, random());
      CHECK(!tree::replace(&f.index, 1000, 1, 1));
      CHECK(tree::get(&f.index, 1000) == tree::NULLVALUEPOINTER);
      uint64_t low = random() % 130;
      uint64_t high = random() % 130;
      if (low > high) std::swap(low, high);
      range(&f, expected, low, high);
    }
    while (!expected.empty()) erase(&f, &expected, expected.begin()->first);
    destroy(&f);
  }
}

void small_permutations() {
  fixture f;
  create(&f);
  model expected;
  // Every insertion/deletion order for four keys, across four rank patterns.
  for (unsigned pattern = 0; pattern < 4; ++pattern) {
    uint64_t insertion[] = {0, 1, 2, 3};
    do {
      uint64_t deletion[] = {0, 1, 2, 3};
      do {
        for (uint64_t key : insertion) {
          unsigned rank = pattern == 0 ? 0 : pattern == 1 ? key : pattern == 2 ? 3 - key : key % 2;
          insert(&f, &expected, key, key + 1, (UINT64_MAX >> rank) - key);
        }
        for (uint64_t key : deletion) erase(&f, &expected, key);
      } while (std::next_permutation(std::begin(deletion), std::end(deletion)));
    } while (std::next_permutation(std::begin(insertion), std::end(insertion)));
  }
  destroy(&f);
}

void endpoints() {
  fixture f;
  create(&f);
  model expected;
  insert(&f, &expected, 0, 1, UINT64_MAX);
  insert(&f, &expected, UINT64_MAX, 2, UINT64_MAX >> 1);
  range(&f, expected, 0, UINT64_MAX);
  range(&f, expected, UINT64_MAX - 1, UINT64_MAX);
  erase(&f, &expected, 0);
  erase(&f, &expected, UINT64_MAX);
  destroy(&f);
}

void allocation_failures() {
  fixture f;
  create(&f);
  model expected;
  insert(&f, &expected, 1, 1, UINT64_MAX);
  // Simulate exhausted leaf storage; a reserved internal node must be returned.
  const uint32_t leaf_watermark = f.nodes.by_rank[0].high_watermark;
  f.nodes.by_rank[0].high_watermark = arena::partition_end(0);
  CHECK(tree::unsafe_associate(&f.index, 2, 2, UINT64_MAX) == tree::OUT_OF_MEMORY);
  f.nodes.by_rank[0].high_watermark = leaf_watermark;
  verify(&f, expected);
  // Simulate exhausted internal rank, with leaf storage still available.
  const uint32_t internal_watermark = f.nodes.by_rank[1].high_watermark;
  const auto free_head = f.nodes.by_rank[1].head;
  f.nodes.by_rank[1].head.next_index = arena::INVALID_INDEX;
  f.nodes.by_rank[1].high_watermark = arena::partition_end(1);
  CHECK(tree::unsafe_associate(&f.index, 2, 2, UINT64_MAX) == tree::OUT_OF_MEMORY);
  f.nodes.by_rank[1].high_watermark = internal_watermark;
  f.nodes.by_rank[1].head = free_head;
  verify(&f, expected);
  erase(&f, &expected, 1);
  destroy(&f);

  create(&f);
  // An empty tree needs only a leaf, even if its would-be internal rank is full.
  f.nodes.by_rank[31].high_watermark = arena::partition_end(31);
  CHECK(tree::unsafe_associate(&f.index, 1, 1, 0) == tree::NO_ERRORS);
  f.nodes.by_rank[31].high_watermark = arena::partition_begin(31);
  expected.emplace(1, record{1, 0});
  verify(&f, expected);
  erase(&f, &expected, 1);
  destroy(&f);

  // The highest rank has exactly one internal slot; failure must preserve it.
  create(&f);
  insert(&f, &expected, 1, 1, 0);
  insert(&f, &expected, 2, 2, 0);
  CHECK(tree::unsafe_associate(&f.index, 3, 3, 0) == tree::OUT_OF_MEMORY);
  verify(&f, expected);
  erase(&f, &expected, 2);
  insert(&f, &expected, 3, 3, 0);
  erase(&f, &expected, 1);
  erase(&f, &expected, 3);
  destroy(&f);
}
}

int main() {
  tree_tests::sequences();
  tree_tests::random_operations();
  tree_tests::small_permutations();
  tree_tests::endpoints();
  tree_tests::allocation_failures();
  std::puts("PASS: tree sequences, 40,000 random operations, 2,304 permutation cases, endpoints, and allocation rollback");
}
