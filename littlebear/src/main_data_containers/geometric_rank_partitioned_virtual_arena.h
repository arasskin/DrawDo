#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include "virtual_mapping.h"

namespace geometric_rank_partioned_virtual_slab_arena {

constexpr size_t SIZE_OF_SLAB = 16;
constexpr size_t SIZE = UINT32_MAX * SIZE_OF_SLAB;
constexpr size_t NUMBER_OF_RANKS = 32;
constexpr uint32_t INVALID_INDEX = UINT32_MAX;

struct slab {
  uint32_t next_index = INVALID_INDEX;
};

struct allocator {
  struct head_and_watermark {
    slab head;
    uint32_t high_watermark;
  } by_rank[NUMBER_OF_RANKS];
  uint8_t *start;
};

struct maybe_allocator {allocator a; bool has_error;};

constexpr uint32_t partition_begin(uint8_t rank) {
  return UINT32_MAX - (static_cast<uint64_t>(UINT32_MAX) >> rank);
}

constexpr uint32_t partition_end(uint8_t rank) {
  return UINT32_MAX - (static_cast<uint64_t>(UINT32_MAX) >> (rank + 1));
}

inline maybe_allocator create() {
  if (void *start = virtual_mapping::create(SIZE); start != nullptr) {
    allocator a{};
    a.start = (uint8_t *)start;
    for (uint8_t rank = 0; rank < NUMBER_OF_RANKS; ++rank)
      a.by_rank[rank].high_watermark = partition_begin(rank);
    return {.a = a, .has_error = false};
  }

  return {.a = {}, .has_error = true};
}

inline void destroy(allocator *a) {
  virtual_mapping::destroy(a->start, SIZE);
  a->start = nullptr;
}

inline void *address(allocator *a, uint32_t index) {
  return (void *)(a->start + (index * SIZE_OF_SLAB));
}

inline uint32_t reserve(allocator *a, uint8_t rank) {
  if (rank >= NUMBER_OF_RANKS || a->start == nullptr) return INVALID_INDEX;
  allocator::head_and_watermark *rank_allocator = &a->by_rank[rank];

  if (uint32_t free_slab_index = rank_allocator->head.next_index; free_slab_index != INVALID_INDEX) {
    rank_allocator->head.next_index = ((slab *)(address(a, free_slab_index)))->next_index;
    return free_slab_index;
  }

  if (const uint32_t high_watermark_index = rank_allocator->high_watermark;
      high_watermark_index < partition_end(rank)) {
    rank_allocator->high_watermark++;
    return high_watermark_index;
  }

  return INVALID_INDEX;
}

inline uint32_t index(allocator *a, void *address) {
  return ((uintptr_t)address - (uintptr_t)a->start) / SIZE_OF_SLAB;
}

inline uint_fast8_t partition(uint32_t index) {
  return std::countl_zero<uint32_t>(~index);
}

inline void release(allocator *a, uint32_t index) {
  allocator::head_and_watermark *rank_allocator = &a->by_rank[partition(index)];
  slab *free_slab = (slab *)address(a, index);
  free_slab->next_index = rank_allocator->head.next_index;
  rank_allocator->head.next_index = index;
}

}
