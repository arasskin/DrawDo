#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cassert>

namespace slab_arena {

struct slab {
  slab *next = nullptr;
};

struct allocator {
  uint8_t *start;
  uint8_t *high_watermark;
  uint8_t *end;
  size_t slab_size;
  slab free_slab_head;
};

inline bool assertions(allocator *a) {
  assert(a->start < a->end);
  assert(a->start <= a->high_watermark);
  assert(a->high_watermark <= a->end);
  assert(a->slab_size == 4 || a->slab_size == 8 || a->slab_size == 16 || a->slab_size == 32 || a->slab_size == 64 || a->slab_size == 128);
  assert((a->end - a->start) % a->slab_size == 0);
  assert((a->end - a->high_watermark) % a->slab_size == 0);
  return true;
}

struct maybe_allocator {
  allocator a;
  bool has_error;
};

inline maybe_allocator create(size_t slab_size, size_t bytes) {
  assert(bytes % slab_size == 0);
  assert(slab_size == 4 || slab_size == 8 || slab_size == 16 || slab_size == 32 || slab_size == 64 || slab_size == 128);
  if (void *arena_start = malloc(bytes); arena_start != nullptr) {
    allocator a = {.start = (uint8_t *)arena_start,
                   .high_watermark = (uint8_t *)arena_start,
                   .end = (uint8_t *)arena_start + bytes,
                   .slab_size = slab_size,
                   .free_slab_head = {},};
    assert(assertions(&a));
    return maybe_allocator{a, false};
  } else return {{}, true};
}

inline void destroy(allocator *a) {
  assert(assertions(a));
  free(a->start);
}

inline void *reserve(allocator *a) {
  assert(assertions(a));
  if (slab *free_slab = a->free_slab_head.next; free_slab != nullptr) {
    a->free_slab_head.next = free_slab->next;
    assert(assertions(a));
    return free_slab;
  }

  if (uint8_t *high_watermark = a->high_watermark; high_watermark < a->end) {
    a->high_watermark += a->slab_size;
    assert(assertions(a));
    return high_watermark;
  }

  return nullptr;
}

inline void release(allocator *a, void *address) {
  assert(assertions(a));
  slab *new_free_slab = (slab *)address;
  new_free_slab->next = a->free_slab_head.next;
  a->free_slab_head.next = new_free_slab;
  assert(assertions(a));
}

}
