#pragma once
#include <cstdint>
#include <sys/types.h>
#include <sys/mman.h>
#include <cstdio>

//this allocator gives us endless (32GB) 8 byte slabs, if we run out of slabs, the program should crash, this is intentional and simple
namespace endless_virtual_slab_allocator {

constexpr uint32_t INVALID_INDEX = UINT32_MAX;
constexpr size_t SIZE_OF_SLAB = 8;
constexpr size_t DEFAULT_SIZE = 32ULL * 1024 * 1024 * 1024; //32GB, this is the amount of memory that can be covered by 4byte indices to 8byte slabs
constexpr size_t PAGE_SIZE = 4096; //we allocate an extra page to ensure we segfault if we get past our 32gb, this PAGE_SIZE has nothing to do with a buffer pool manager implementation or anything like that

struct slab {
  uint32_t next_index = INVALID_INDEX;
};

struct allocator {
  slab head;
  uint32_t high_watermark = 0;
  uint8_t *start;
};

struct maybe_allocator {allocator a; bool has_error;};

inline maybe_allocator create() {
  if (void *start = mmap(nullptr, DEFAULT_SIZE + PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      start != MAP_FAILED) {
    if (mprotect((uint8_t *)start + DEFAULT_SIZE, PAGE_SIZE, PROT_NONE) == 0) {
      allocator  a = allocator{};
      a.start = (uint8_t *)start;
      return {.a = a, .has_error = false};
    } else perror("endless_virtual_slab_allocator failed mprotect");
  } else perror("endless_virtual_slab_allocator failed mmap");

  return {.a = {}, .has_error = true};
}

inline void *address(allocator *a, uint32_t index) {
  return (void *)(a->start + (index * SIZE_OF_SLAB));
}

inline uint32_t reserve(allocator *a) {
  if (uint32_t free_slab_index = a->head.next_index; free_slab_index != INVALID_INDEX) {
    a->head.next_index = ((slab *)(address(a, free_slab_index)))->next_index;
    return free_slab_index;
  }

  return a->high_watermark++;
}

inline uint32_t index(allocator *a, void *address) {
  return ((uintptr_t)address - (uintptr_t)a->start) / SIZE_OF_SLAB;
}

inline void release(allocator *a, uint32_t index) {
  slab *free_slab = (slab *)address(a, index);
  free_slab->next_index = a->head.next_index;
  a->head.next_index = index;
}

}
