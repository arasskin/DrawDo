#pragma once
#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#include "../../../third_party/xxhash/xxhash.h"
#include "../../../datafriend/include/datafriend/wide_tree.h"
#include <type_traits>

// LittleBear's explicit-lifetime facade over the shared mutable wide tree.
// Headers may relocate with the namespace table; nodes and the pool stay put.
namespace record_index {
using pool = datafriend::slab_pool;
using key = uint64_t;
using value_pointer = uint64_t;
using record = datafriend::record;
using key_range = datafriend::key_range;
constexpr value_pointer NULLVALUEPOINTER = 0;
constexpr uint64_t OFFSET_MASK = UINT64_C(0x0000FFFFFFFFFFFF);
constexpr key MIN_KEY = 0, MAX_KEY = UINT64_MAX;
inline value_pointer vpointer_create(uint64_t offset, uint16_t size) {
  return (offset & OFFSET_MASK) | (uint64_t(size) << 48);
}
inline uint64_t vpointer_get_offset(value_pointer p) {
  return p & OFFSET_MASK;
}
inline uint16_t vpointer_get_size(value_pointer p) {
  return uint16_t(p >> 48);
}

inline uint64_t key_hash(key k) {
  uint8_t bytes[8];
  for (unsigned i = 0; i < 8; ++i)
    bytes[i] = uint8_t(k >> (56 - 8 * i));
  return XXH64(bytes, sizeof(bytes), 0);
}
inline unsigned key_rank(key k) {
  return std::countr_zero(uint32_t(key_hash(k))) / 4;
}
struct allocator_ref {
  ct_pool *nodes;
  void *allocate(size_t bytes) { return ct_pool_allocate(nodes, bytes); }
  void deallocate(void *p, size_t bytes) { ct_pool_deallocate(nodes, p, bytes); }
};
using implementation = datafriend::basic_wide_tree<allocator_ref, false>;
using header = lb_index;
static_assert(sizeof(header) == 24 && std::is_trivially_copyable_v<header>);

// The table owns the root. A temporary view borrows it and returns it even when
// a mutation throws. No heap allocation or reference counting for the view.
class access : public implementation {
  header *header_;

public:
  explicit access(header *h)
      : implementation(key_hash, allocator_ref{h->node_allocator}), header_(h) {
    adopt_state({static_cast<decltype(state{}.root)>(h->root), h->rank});
  }
  ~access() {
    const auto state = take_state();
    header_->root = state.root;
    header_->rank = state.rank;
  }
};
inline header create(pool *nodes) {
  return lb_index_create(nodes);
}
inline bool is_empty(const header *h) {
  return !h->root;
}
inline void destroy(header *h) {
  implementation owned(key_hash, allocator_ref{h->node_allocator});
  owned.adopt_state({static_cast<decltype(implementation::state{}.root)>(h->root), h->rank});
  *h = {};
}
inline std::optional<record> find(header *h, key k) {
  return access(h).get(k);
}
inline value_pointer get(header *h, key k) {
  auto r = find(h, k);
  return r ? r->value : 0;
}
inline bool replace(header *h, key k, value_pointer value, uint64_t fingerprint) {
  return access(h).replace_existing({k, value, fingerprint});
}
enum error_codes { NO_ERRORS, OUT_OF_MEMORY };
// k must be absent. A failure leaves all records and summaries unchanged;
// successful capacity growth on the path may be retained for a retry.
inline error_codes unsafe_associate(header *h, key k, value_pointer value, uint64_t fingerprint) {
  try {
    access(h).set({k, value, fingerprint});
    return NO_ERRORS;
  } catch (const std::bad_alloc &) {
    return OUT_OF_MEMORY;
  }
}
enum class erase_result { removed, missing, no_memory };
inline erase_result erase(header *h, key k) {
  try {
    return access(h).erase(k) ? erase_result::removed : erase_result::missing;
  } catch (const std::bad_alloc &) {
    return erase_result::no_memory;
  }
}
inline uint64_t get_full_fingerprint(header *h) {
  return access(h).fingerprint();
}
inline uint64_t get_range_fingerprint(header *h, key low, key high) {
  return access(h).summarize({low, high}).hash;
}
inline datafriend::range_summary summarize(header *h, key_range r = {}) {
  return access(h).summarize(r);
}
inline uint64_t size(header *h) {
  return access(h).size();
}
inline std::optional<record> select(header *h, uint64_t rank) {
  return access(h).select(rank);
}
inline std::vector<record> list(header *h, key_range r = {}, size_t limit = SIZE_MAX) {
  return access(h).list(r, limit);
}
inline std::vector<uint64_t> cuts(header *h, key_range r, size_t fanout) {
  return access(h).cuts(r, fanout);
}
inline void validate(header *h) {
  access(h).validate();
}
} // namespace record_index
