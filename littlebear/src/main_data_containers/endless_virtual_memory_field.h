#pragma once
#include <cstdint>
#include <sys/types.h>
#include <sys/mman.h>
#include <cstdio>

namespace endless_virtual_memory_field {

using entry = uint64_t;
constexpr size_t SIZE = UINT32_MAX * sizeof(entry);
constexpr size_t PAGE_SIZE = 4096;

inline uint64_t *create() {
  if (void *start = mmap(nullptr, SIZE + PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      start != MAP_FAILED) {
    if (mprotect((uint8_t *)start + SIZE, PAGE_SIZE, PROT_NONE) == 0) {
      return (uint64_t *)start;
    } else perror("endless_virtual_memory_field failed mprotect");
  } else perror("endless_virtual_memory_field failed mmap");

  return nullptr;
}

}
