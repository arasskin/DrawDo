#pragma once
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include "growing_slab_arena.h"

namespace size_class_file_heap {

struct slab {
  int next_slab_index = -1;
  uint64_t file_offset = 0;
};

inline slab *to_slab(int index, growing_slab_arena::allocator *a) {
  return (slab *)growing_slab_arena::voidp(a, index);
}

// Class 0 is 32 bytes; eight subdivisions per doubling end at 65,536.
constexpr size_t NUMBER_OF_SIZE_CLASSES = 89;
constexpr uint8_t INVALID_SIZE_CLASS = UINT8_MAX;
constexpr size_t MAX_VALUE_SIZE = UINT16_MAX;

struct allocator {
  uint64_t high_watermark = 0;
  uint64_t limit = 0;
  slab size_class_heads[NUMBER_OF_SIZE_CLASSES];
  growing_slab_arena::allocator slab_allocator;
};

enum allocator_error {
  no_error,
  unable_to_open_file,
  unable_to_allocate_metadata,
};

inline void print_allocator_error(allocator_error error) {
  switch (error) {
    case no_error: printf("No allocator error :)\n"); break;
    case unable_to_open_file: printf("Unable to open file while creating allocator\n"); break;
    case unable_to_allocate_metadata: printf("Unable to allocate file-heap metadata\n"); break;
  }
}

struct file_and_allocator {
  int file_descriptor;
  allocator file_allocator;
  bool has_error;
};

inline file_and_allocator create(const char *file_name, const size_t size) {
  allocator_error error = no_error;
  if (int fd = open(file_name, O_CREAT | O_RDWR | O_CLOEXEC, 0644); fd != -1) {
    auto metadata = growing_slab_arena::create(sizeof(slab), 8 * 1024);
    if (!growing_slab_arena::bad(&metadata)) return file_and_allocator {
      .file_descriptor = fd,
      .file_allocator = allocator{.high_watermark = {},
                                  .limit = size,
                                  .size_class_heads = {},
                                  .slab_allocator = metadata},
      .has_error = false};
    close(fd);
    error = unable_to_allocate_metadata;
  } else error = unable_to_open_file;

  print_allocator_error(error);
  return file_and_allocator{.file_descriptor = -1, .file_allocator = {}, .has_error = true};
}

inline void destroy(file_and_allocator *file) {
  growing_slab_arena::destroy(&file->file_allocator.slab_allocator);
  if (file->file_descriptor >= 0) close(file->file_descriptor);
  file->file_descriptor = -1;
}

inline uint8_t size_class_index(size_t bytes) {
  if (bytes == 0 || bytes > MAX_VALUE_SIZE) return INVALID_SIZE_CLASS;
  if (bytes <= 32) return 0;
  const uint32_t last_byte = static_cast<uint32_t>(bytes - 1);
  const unsigned log2 = 31 - std::countl_zero(last_byte);
  const unsigned subdivision = (last_byte >> (log2 - 3)) & 7;
  return 1 + (log2 - 5) * 8 + subdivision;
}

inline uint32_t slot_size(uint8_t index) {
  if (index >= NUMBER_OF_SIZE_CLASSES) return 0;
  if (index == 0) return 32;
  --index;
  const unsigned log2 = index / 8 + 5;
  return (1u << log2) + ((index % 8 + 1u) << (log2 - 3));
}

// Zero means unsupported; the physical slot may exceed the 16-bit payload length.
inline uint32_t next_smallest_slot_size(size_t bytes) {
  return slot_size(size_class_index(bytes));
}

struct maybe_offset {
  uint64_t offset;
  bool has_error;
};

// Offset zero is valid. Payloads must contain between 1 and 65,535 bytes.
inline maybe_offset reserve(allocator *a, size_t size) {
  uint8_t size_class = size_class_index(size);
  if (size_class == INVALID_SIZE_CLASS) return {0, true};
  slab *size_allocator = &a->size_class_heads[size_class];

  if (int head_index = size_allocator->next_slab_index; head_index != -1) {
    slab *head = to_slab(head_index, &a->slab_allocator);
    uint64_t offset = head->file_offset;
    size_allocator->next_slab_index = head->next_slab_index;
    growing_slab_arena::release(&a->slab_allocator, head_index);
    return {offset, false};
  }

  if (uint32_t fitting_size = slot_size(size_class);
      a->high_watermark <= a->limit && fitting_size <= a->limit - a->high_watermark) {
    uint64_t offset = a->high_watermark;
    a->high_watermark += fitting_size;
    return {offset, false};
  }

  return {0, true};
}

// Release a live allocation exactly once with its original payload size.
// On metadata exhaustion it remains allocated; callers can retry the release.
[[nodiscard]] inline bool release(allocator *a, uint64_t data, size_t size) {
  const uint8_t size_class = size_class_index(size);
  if (size_class == INVALID_SIZE_CLASS || data >= a->high_watermark ||
      slot_size(size_class) > a->high_watermark - data) return false;
  slab *size_allocator_slab_list_head = &a->size_class_heads[size_class];
  int new_free_slab_index = growing_slab_arena::reserve(&a->slab_allocator);
  if (new_free_slab_index == -1) return false;
  slab *new_slab_head = to_slab(new_free_slab_index, &a->slab_allocator);
  new_slab_head->file_offset = data;
  new_slab_head->next_slab_index = size_allocator_slab_list_head->next_slab_index;
  size_allocator_slab_list_head->next_slab_index = new_free_slab_index;
  return true;
}

}
