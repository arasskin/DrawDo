#pragma once
#include <cstdlib>
#include <cstdint>
#include <climits>
#include <cstddef>
#include <sys/types.h>

namespace growing_slab_arena {

struct slab {
  int next_index = -1;
};

struct allocator {
  uint8_t *start;
  int high_watermark_index;
  int size;
  size_t slab_size;
  slab free_slab_head;
};

inline bool bad(allocator *a) {return a->start == nullptr;}

inline allocator create(size_t slab_size, int bytes) {
  if (slab_size < sizeof(slab) || slab_size % alignof(slab) != 0 ||
      bytes <= 0 || slab_size > static_cast<size_t>(bytes)) return {};
  void *arena_start = malloc(bytes);
  return allocator {
    .start = (uint8_t *)arena_start,
    .high_watermark_index = 0,
    .size = bytes / (int)slab_size,
    .slab_size = slab_size,
    .free_slab_head = {}};
}

inline void destroy(allocator *a){free(a->start); *a = {};}

inline bool grow(allocator *a) {
  if (bad(a) || a->size <= 0 || a->size > INT_MAX / 2 ||
      a->slab_size > SIZE_MAX / (static_cast<size_t>(a->size) * 2)) return false;
  const int new_size = a->size * 2;
  if (void *new_start = realloc(a->start, new_size * a->slab_size);
      new_start != nullptr) {
    a->start = (uint8_t *)new_start;
    a->size = new_size;
    return true;
  }
  return false;
}

inline int reserve(allocator *a) {
  if (bad(a)) return -1;
  if (a->free_slab_head.next_index != -1) {
    int free_slab_index = a->free_slab_head.next_index;
    a->free_slab_head.next_index = ((slab *)(a->start + free_slab_index * a->slab_size))->next_index;
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
  new_slab->next_index = a->free_slab_head.next_index;
  a->free_slab_head.next_index = index;
}

}
