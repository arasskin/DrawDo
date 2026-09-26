#pragma once
#include "../xxhash.h"
#include "augmented_tree.h"
#include <cassert>
#include <cstdlib>

namespace hash_table {

// Internal namespace fingerprints; the owner/public-key wire format is separate.
using namespace_identifier = XXH128_hash_t;
using key = namespace_identifier;
using value = augmented_tree::header;
inline bool operator==(key a, key b) { return a.high64 == b.high64 && a.low64 == b.low64; }

constexpr key EMPTY{UINT64_MAX, UINT64_MAX};
constexpr size_t BUCKET_SIZE = 8;
constexpr size_t DEFAULT_BUCKET_COUNT = 128;
// 20 MiB of buckets at the ceiling; resizing temporarily holds both arrays.
constexpr size_t DEFAULT_MAX_BUCKET_COUNT = 65536;

struct alignas(64) bucket { key keys[BUCKET_SIZE]; value values[BUCKET_SIZE]; };
struct two_way {
  bucket *data = nullptr;
  size_t bucket_count = 0;
  size_t inserted_item_count = 0;
  size_t max_bucket_count = 0;
  // Keep the all-ones key usable without adding occupancy metadata to buckets.
  value empty_key_value{};
  bool has_empty_key = false;
};

// Allocation injection is confined to creation/growth, for failure tests.
// A custom allocator must return memory that std::free can release.
using allocate_fn = void *(*)(size_t alignment, size_t bytes);

inline bool valid_bucket_count(size_t count) {
  return count != 0 && (count & (count - 1)) == 0 && count <= SIZE_MAX / sizeof(bucket);
}

inline void reset_keys(bucket *data, size_t count) {
  for (size_t i = 0; i < count; ++i)
    for (size_t j = 0; j < BUCKET_SIZE; ++j) data[i].keys[j] = EMPTY;
}

inline two_way create(size_t count = DEFAULT_BUCKET_COUNT,
                      size_t maximum = DEFAULT_MAX_BUCKET_COUNT,
                      allocate_fn allocate = std::aligned_alloc) {
  if (!valid_bucket_count(count) || !valid_bucket_count(maximum) || count > maximum) return {};
  auto *data = (bucket *)allocate(alignof(bucket), sizeof(bucket) * count);
  if (data == nullptr) return {};
  reset_keys(data, count);
  return {.data = data, .bucket_count = count, .inserted_item_count = 0, .max_bucket_count = maximum};
}

inline bool bad(const two_way *table) { return table->data == nullptr; }
inline void destroy(two_way *table) { std::free(table->data); *table = {}; }

inline size_t occupied(const bucket *b) {
  size_t count = 0;
  while (count < BUCKET_SIZE && b->keys[count] != EMPTY) ++count;
  return count;
}

// Returns the stored header, including an empty tree, or nullptr for absence.
// Re-look up after structural table mutations: growth/deletion can move headers.
inline value *get(two_way *table, key k) {
  if (bad(table)) return nullptr;
  if (k == EMPTY) return table->has_empty_key ? &table->empty_key_value : nullptr;
  bucket *first = &table->data[k.high64 & (table->bucket_count - 1)];
  bucket *second = &table->data[k.low64 & (table->bucket_count - 1)];
  for (size_t i = 0; i < BUCKET_SIZE; ++i) {
    if (first->keys[i] == k) return &first->values[i];
    if (second->keys[i] == k) return &second->values[i];
    if (first->keys[i] == EMPTY && second->keys[i] == EMPTY) break;
  }
  return nullptr;
}

inline bool contains(two_way *table, key k) { return get(table, k) != nullptr; }

// Unsafe: k must already exist.
inline value unsafe_get(two_way *table, key k) {
  value *found = get(table, k);
  assert(found != nullptr);
  return *found;
}

// Internal insertion: ordinary key known absent, no allocation or resizing.
inline bool insert_into(bucket *data, size_t count, key k, value v) {
  bucket *first = &data[k.high64 & (count - 1)];
  bucket *second = &data[k.low64 & (count - 1)];
  size_t first_size = occupied(first);
  size_t second_size = first == second ? first_size : occupied(second);
  if (first_size == BUCKET_SIZE && second_size == BUCKET_SIZE) return false;
  bucket *target = first_size <= second_size ? first : second;
  size_t slot = first_size <= second_size ? first_size : second_size;
  target->keys[slot] = k;
  target->values[slot] = v;
  return true;
}

// Rebuild separately, then publish only when all old entries and the optional
// new entry fit. On any failure, the original table and header addresses remain.
inline bool rebuild(two_way *table, const key *extra_key, const value *extra_value,
                    allocate_fn allocate) {
  if (bad(table)) return false;
  size_t candidate_count = table->bucket_count;
  while (candidate_count < table->max_bucket_count) {
    candidate_count *= 2; // both counts were validated powers of two
    auto *candidate = (bucket *)allocate(alignof(bucket), candidate_count * sizeof(bucket));
    if (candidate == nullptr) return false;
    reset_keys(candidate, candidate_count);
    bool fits = true;
    for (size_t i = 0; i < table->bucket_count && fits; ++i) {
      const bucket *old = &table->data[i];
      for (size_t j = 0; j < BUCKET_SIZE && old->keys[j] != EMPTY; ++j) {
        if (!insert_into(candidate, candidate_count, old->keys[j], old->values[j])) {
          fits = false;
          break;
        }
      }
    }
    if (fits && extra_key != nullptr)
      fits = insert_into(candidate, candidate_count, *extra_key, *extra_value);
    if (fits) {
      std::free(table->data);
      table->data = candidate;
      table->bucket_count = candidate_count;
      if (extra_key != nullptr) ++table->inserted_item_count;
      return true;
    }
    std::free(candidate);
  }
  return false;
}

[[nodiscard]] inline bool grow(two_way *table, allocate_fn allocate = std::aligned_alloc) {
  return rebuild(table, nullptr, nullptr, allocate);
}

// Unsafe: k must be absent. false means allocation/capacity failure; no mutation.
[[nodiscard]] inline bool unsafe_insert(two_way *table, key k, value v,
                                        allocate_fn allocate = std::aligned_alloc) {
  if (bad(table)) return false;
  if (k == EMPTY) {
    table->empty_key_value = v;
    table->has_empty_key = true;
    ++table->inserted_item_count;
    return true;
  }
  if (!insert_into(table->data, table->bucket_count, k, v))
    return rebuild(table, &k, &v, allocate);
  ++table->inserted_item_count;
  return true;
}

[[nodiscard]] inline bool associate(two_way *table, key k, value v,
                                    allocate_fn allocate = std::aligned_alloc) {
  if (bad(table)) return false;
  if (k == EMPTY) {
    if (!table->has_empty_key) ++table->inserted_item_count;
    table->has_empty_key = true;
    table->empty_key_value = v;
    return true;
  }
  bucket *choices[] = {&table->data[k.high64 & (table->bucket_count - 1)],
                       &table->data[k.low64 & (table->bucket_count - 1)]};
  size_t sizes[2];
  for (size_t choice = 0; choice < 2; ++choice) {
    if (choice == 1 && choices[0] == choices[1]) { sizes[1] = sizes[0]; break; }
    bucket *b = choices[choice];
    size_t i = 0;
    for (; i < BUCKET_SIZE && b->keys[i] != EMPTY; ++i) {
      if (b->keys[i] == k) { b->values[i] = v; return true; }
    }
    sizes[choice] = i;
  }
  if (sizes[0] == BUCKET_SIZE && sizes[1] == BUCKET_SIZE)
    return rebuild(table, &k, &v, allocate);
  size_t choice = sizes[0] <= sizes[1] ? 0 : 1;
  choices[choice]->keys[sizes[choice]] = k;
  choices[choice]->values[sizes[choice]] = v;
  ++table->inserted_item_count;
  return true;
}

inline bool dissociate(two_way *table, key k) {
  if (bad(table)) return false;
  if (k == EMPTY) {
    if (!table->has_empty_key) return false;
    table->has_empty_key = false;
    table->empty_key_value = {};
    --table->inserted_item_count;
    return true;
  }
  bucket *choices[] = {&table->data[k.high64 & (table->bucket_count - 1)],
                       &table->data[k.low64 & (table->bucket_count - 1)]};
  for (size_t choice = 0; choice < 2; ++choice) {
    if (choice == 1 && choices[0] == choices[1]) break;
    bucket *b = choices[choice];
    const size_t size = occupied(b);
    for (size_t i = 0; i < size; ++i) {
      if (b->keys[i] != k) continue;
      // Packed buckets need no ordering; replace with the last occupied slot.
      b->keys[i] = b->keys[size - 1];
      b->values[i] = b->values[size - 1];
      b->keys[size - 1] = EMPTY;
      b->values[size - 1] = {};
      --table->inserted_item_count;
      return true;
    }
  }
  return false;
}

// Unsafe: k must already exist.
inline void unsafe_dissociate(two_way *table, key k) {
  const bool removed = dissociate(table, k);
  assert(removed);
  (void)removed;
}

// Membership only: callers retain ownership of tree nodes and value storage.
inline void clear(two_way *table) {
  reset_keys(table->data, table->bucket_count);
  table->inserted_item_count = 0;
  table->has_empty_key = false;
  table->empty_key_value = {};
}

inline size_t count(const two_way *table) { return table->inserted_item_count; }
inline size_t memory_usage(const two_way *table) { return sizeof(two_way) + sizeof(bucket) * table->bucket_count; }

}
