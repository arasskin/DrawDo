#pragma once
#include <cstdlib>
#include <cstdint>
#include <sys/types.h>

namespace growing_slab_arena {

struct slab {
  slab *next = nullptr;
};

struct allocator {
  u_int8_t *start;
  int high_watermark_index;
  int size;
  size_t slab_size;
  slab free_slab_head;
};

inline bool bad(allocator *a) {return a->start == nullptr;}

inline allocator create(size_t slab_size, int bytes) {
  void *arena_start = malloc(bytes);
  return allocator {
    .start = (uint8_t *)arena_start,
    .high_watermark_index = 0,
    .size = bytes / (int)slab_size,
    .slab_size = slab_size,
    .free_slab_head = {}};
}

inline void destroy(allocator *a){free(a->start);}

inline bool grow(allocator *a) {
  a->size *= 2;
  if (void *new_start = realloc(a->start, a->size * a->slab_size);
      new_start != nullptr) {
    a->start = (uint8_t *)new_start;
    return true;
  }
  return false;
}

inline int reserve(allocator *a) {
  if (a->free_slab_head.next != nullptr) {
    int free_slab_index = ((uint8_t *)a->free_slab_head.next - a->start) / a->slab_size;
    a->free_slab_head.next = a->free_slab_head.next->next;
    return free_slab_index;
  }

  if (a->high_watermark_index < a->size) {
    int high_watermark_index = a->high_watermark_index;
    a->high_watermark_index++;
    return high_watermark_index;
  }

  if (grow(a)) {
    int high_watermark_index = a->high_watermark_index;
    a->high_watermark_index++;
    return high_watermark_index;
  }

  return -1;
}

inline void *voidp(allocator *a, int index) {
  return (void *)(a->start + index * a->slab_size);
}

inline void release(allocator *a, int index) {
  slab *new_slab = (slab *)voidp(a, index);
  new_slab->next = a->free_slab_head.next;
  a->free_slab_head.next = new_slab;
}

}
