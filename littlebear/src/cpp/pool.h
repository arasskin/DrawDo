#pragma once
namespace slab_pool {
using slab = ct_free_slot;
using allocator = ct_arena;
inline bool bad(allocator *a) {
  return ct_arena_bad(a);
}
inline allocator create(size_t n, int bytes) {
  return ct_arena_create(n, bytes);
}
inline void destroy(allocator *a) {
  ct_arena_destroy(a);
}
inline bool grow(allocator *a) {
  return ct_arena_grow(a);
}
inline int reserve(allocator *a) {
  return ct_arena_reserve(a);
}
inline void *voidp(allocator *a, int i) {
  return ct_arena_at(a, i);
}
inline void release(allocator *a, int i) {
  ct_arena_release(a, i);
}
} // namespace slab_pool
