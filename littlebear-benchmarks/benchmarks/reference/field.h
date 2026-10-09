#pragma once
#include <cstdint>
#include <sys/types.h>
#include <sys/mman.h>
#include <cstdio>
#include "mapping.h"

namespace hash_field {

using entry = uint64_t;
constexpr size_t SIZE = UINT32_MAX * sizeof(entry);

inline uint64_t *create() {
  return static_cast<entry *>(virtual_mapping::create(SIZE));
}

inline void destroy(entry *start) {
  virtual_mapping::destroy(start, SIZE);
}

}
