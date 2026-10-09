#pragma once
#include "index.h"
#include "../../third_party/unordered_dense/include/ankerl/unordered_dense.h"
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>

namespace hash_table {

// Internal, uniformly distributed namespace fingerprints; wire identity is separate.
using namespace_identifier = XXH128_hash_t;
using key = namespace_identifier;
using value = record_index::header;
inline bool operator==(key a, key b) noexcept { return a.high64 == b.high64 && a.low64 == b.low64; }
struct equal { bool operator()(key a, key b) const noexcept { return a == b; } };
struct prehashed {
  using is_avalanching = void;
  uint64_t operator()(key k) const noexcept { return k.low64; }
};

// All keys are ordinary entries, including the old sentinel.
constexpr key ALL_ONES{UINT64_MAX, UINT64_MAX};
constexpr size_t DEFAULT_INITIAL_CAPACITY = 0; // allocate lazily

// The callback must return free-compatible storage or nullptr. Used for both
// dense values and index groups, including later growth. Alignment/size are
// suitable for aligned_alloc; callback state must outlive the table.
using allocate_fn = void *(*)(size_t alignment, size_t bytes);
inline void *system_allocate(size_t alignment, size_t bytes) {
  return std::aligned_alloc(alignment, bytes);
}
template<class T> struct allocator {
  using value_type = T;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;
  allocate_fn allocate_bytes = system_allocate;
  allocator() = default;
  explicit allocator(allocate_fn fn) : allocate_bytes(fn) {}
  template<class U> allocator(const allocator<U>& other) : allocate_bytes(other.allocate_bytes) {}
  T *allocate(size_t n) {
    constexpr size_t alignment = std::max(alignof(T), alignof(void*));
    if (n > (SIZE_MAX - (alignment - 1)) / sizeof(T)) throw std::bad_alloc{};
    const size_t bytes = (n * sizeof(T) + alignment - 1) & ~(alignment - 1);
    auto *p = allocate_bytes(alignment, bytes);
    if (!p) throw std::bad_alloc{};
    return static_cast<T*>(p);
  }
  void deallocate(T *p, size_t) noexcept { std::free(p); }
  template<class U> bool operator==(const allocator<U>& other) const noexcept {
    return allocate_bytes == other.allocate_bytes;
  }
};
using map = ankerl::unordered_dense::map<key, value, prehashed, equal,
                                       allocator<std::pair<key, value>>>;
struct directory {
  map entries;
  bool initialized = false;
  directory() = default;
  explicit directory(allocate_fn fn) : entries(allocator<std::pair<key, value>>(fn)) {}
  directory(const directory&) = delete; // headers refer to uniquely owned trees
  directory& operator=(const directory&) = delete;
  directory(directory&&) = default;
  directory& operator=(directory&&) = default;
};

inline bool valid_capacity(size_t initial) { return initial <= map::max_size(); }
inline directory create(size_t initial = DEFAULT_INITIAL_CAPACITY,
                        allocate_fn allocate = system_allocate) {
  if (!allocate || !valid_capacity(initial)) return {};
  directory t(allocate);
  try {
    if (initial) t.entries.reserve(initial);
    t.initialized = true;
    return t;
  } catch (const std::bad_alloc&) { return {}; }
  catch (const std::overflow_error&) { return {}; }
}
inline bool bad(const directory *t) { return !t->initialized; }

// Membership only: caller destroys tree roots before clear/destroy or replacing
// a populated header. C++ destruction releases the directory's backing arrays.
inline void destroy(directory *t) {
  std::destroy_at(t);
  std::construct_at(t);
}
inline void clear(directory *t) { t->entries.clear(); }
inline size_t count(const directory *t) { return t->entries.size(); }
inline size_t memory_usage(const directory *t) {
  return sizeof(directory) + t->entries.index_bytes() +
         t->entries.values().capacity() * sizeof(map::value_type);
}
template<class F> void for_each(directory *t, F&& f) {
  for (auto& [k, v] : t->entries) f(k, v);
}

// Any insertion/reserve (even failed growth) or deletion can move headers.
// Never retain these pointers across table mutation; look up by key again.
inline value *get(directory *t, key k) {
  auto it = t->entries.find(k);
  return it == t->entries.end() ? nullptr : &it->second;
}
inline bool contains(directory *t, key k) { return get(t, k) != nullptr; }
inline value unsafe_get(directory *t, key k) {
  auto *v = get(t, k);
  assert(v);
  return *v;
}
[[nodiscard]] inline bool reserve(directory *t, size_t capacity) {
  if (bad(t) || !valid_capacity(capacity)) return false;
  try { t->entries.reserve(capacity); return true; }
  catch (const std::bad_alloc&) { return false; }
  catch (const std::overflow_error&) { return false; }
}
// No application namespace limit. k must be absent. On allocation failure or
// exhaustion of the library's index representation, existing contents survive;
// successfully allocated capacity may remain. Hash/equality/header moves cannot throw.
[[nodiscard]] inline bool unsafe_insert(directory *t, key k, value v) {
  if (bad(t)) return false;
  try { return t->entries.try_emplace(k, v).second; }
  catch (const std::bad_alloc&) { return false; }
  catch (const std::overflow_error&) { return false; }
}
[[nodiscard]] inline bool associate(directory *t, key k, value v) {
  if (auto *existing = get(t, k)) { *existing = v; return true; }
  return unsafe_insert(t, k, v);
}
inline bool dissociate(directory *t, key k) { return t->entries.erase(k) != 0; }
inline void unsafe_dissociate(directory *t, key k) {
  const bool removed = dissociate(t, k);
  assert(removed);
  (void)removed;
}
}
