#pragma once
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include "growing_slab_arena.h"

namespace size_class_file_heap {

// constexpr size_t DEFAULT_

struct slab {
  int64_t next_slab_index = -1;
  void *file_offset = nullptr;
};

inline slab *to_slab(int64_t index, growing_slab_arena::allocator *a) {
  return (slab *)growing_slab_arena::voidp(a, index);
}

constexpr size_t NUMBER_OF_SIZE_CLASSES = 88;

struct allocator {
  uint8_t *high_watermark = 0;
  uint8_t *limit;
  slab size_class_heads[NUMBER_OF_SIZE_CLASSES];
  growing_slab_arena::allocator slab_allocator;
};

enum allocator_error {
  no_error,
  unable_to_open_file,
};

inline void print_allocator_error(allocator_error error) {
  switch (error) {
    case no_error: printf("No allocator error :)\n"); break;
    case unable_to_open_file: printf("Unable to open file while creating allocator\n"); break;
  }
}

struct file_and_allocator {
  int file_descriptor;
  allocator file_allocator;
  bool has_error;
};

inline file_and_allocator create(const char *file_name, const size_t size) {
  allocator_error error = no_error;
  if (int fd = open(file_name, O_CREAT | O_RDWR, 0644); fd != -1) {
    return file_and_allocator {
      .file_descriptor = fd,
      .file_allocator = allocator{.high_watermark = {},
                                  .limit = (uint8_t *)size,
                                  .size_class_heads = {},
                                  .slab_allocator = growing_slab_arena::create(sizeof(slab), 8 * 1024)},
      .has_error = false};
  } else error = unable_to_open_file;

  print_allocator_error(error);
  return file_and_allocator{.file_descriptor = {}, .file_allocator = {}, .has_error = true};
}

inline uint8_t size_class_index(uint16_t bytes) {
      bytes = std::max(bytes, (uint16_t)33);
      bytes--;
      uint_fast8_t highest_set_bit = (sizeof(bytes) * 8) - std::countl_zero<uint16_t>(bytes);
      uint_fast8_t log2 = highest_set_bit - 1;
      uint_fast8_t subdivision = (bytes >> (log2 - 3)) & 0x7;
      return (log2 - 5) * 8 + subdivision;
}

inline uint16_t slot_size(uint8_t index) {
    uint_fast8_t log2 = (index / 8) + 5;
    uint_fast8_t subdivision = index % 8;
    return (1 << log2) | ((subdivision + 1) << (log2 - 3));
}

inline uint16_t next_smallest_slot_size(uint16_t bytes) {
    if (bytes <= 32) return 32;
    return slot_size(size_class_index(bytes));
}

struct maybe_offset {
  void *offset;
  bool has_error;
};

//size must fit in a uint16_t
//we use a maybe offset because offset can be 0 i.e the nullptr
inline maybe_offset reserve(allocator *a, uint16_t size) {
  uint8_t size_class = size_class_index(size);
  slab *size_allocator = &a->size_class_heads[size_class];

  if (int head_index = size_allocator->next_slab_index; head_index != -1) {
    slab *head = to_slab(head_index, &a->slab_allocator);
    void *offset = head->file_offset;
    size_allocator->next_slab_index = head->next_slab_index;
    growing_slab_arena::release(&a->slab_allocator, head_index);
    return {offset, false};
  }

  if (uint16_t fitting_size = slot_size(size_class); a->high_watermark + fitting_size <= a->limit) {
    void *offset = a->high_watermark;
    a->high_watermark += fitting_size;
    return {offset, false};
  }

  return {0, true};
}

inline void release(allocator *a, void *data, uint16_t size) {
  slab *size_allocator_slab_list_head = &a->size_class_heads[size_class_index(size)];
  size_t new_free_slab_index = growing_slab_arena::reserve(&a->slab_allocator);
  slab *new_slab_head = to_slab(new_free_slab_index, &a->slab_allocator);
  new_slab_head->file_offset = data;
  new_slab_head->next_slab_index = size_allocator_slab_list_head->next_slab_index;
  size_allocator_slab_list_head->next_slab_index = new_free_slab_index;
}

}
