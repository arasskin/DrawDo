#pragma once
namespace file_heap {
using slab = lb_heap_slot;
using allocator = lb_heap;
using file_and_allocator = lb_file;
using maybe_offset = lb_offset;
constexpr size_t NUMBER_OF_SIZE_CLASSES = LB_SIZE_CLASSES, MAX_VALUE_SIZE = LB_MAX_VALUE;
constexpr uint8_t INVALID_SIZE_CLASS = LB_INVALID_CLASS;
inline slab *to_slab(int i, slab_pool::allocator *a) {
  return (slab *)ct_arena_at(a, i);
}
inline file_and_allocator create(const char *p, size_t n) {
  return lb_file_create(p, n);
}
inline void destroy(file_and_allocator *f) {
  lb_file_destroy(f);
}
inline uint8_t size_class_index(size_t n) {
  return lb_size_class(n);
}
inline uint32_t slot_size(uint8_t i) {
  return lb_slot_size(i);
}
inline uint32_t next_smallest_slot_size(size_t n) {
  return lb_slot_size(lb_size_class(n));
}
inline maybe_offset reserve(allocator *a, size_t n) {
  return lb_heap_reserve(a, n);
}
inline int prepare_release(allocator *a) {
  return lb_heap_prepare(a);
}
inline void cancel_release(allocator *a, int t) {
  lb_heap_cancel(a, t);
}
inline void release_prepared(allocator *a, uint64_t o, size_t n, int t) {
  lb_heap_release_prepared(a, o, n, t);
}
inline bool release(allocator *a, uint64_t o, size_t n) {
  return lb_heap_release(a, o, n);
}
} // namespace file_heap
