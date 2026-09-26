#pragma once
#include <cstddef>
#include <cstdint>

namespace in_place {

inline void map_u64(uint64_t (*fn)(uint64_t), uint64_t *array, size_t size_of_array) {
  size_t i = 0;
  while (i < size_of_array) {
    array[i] = fn(array[i]);
    ++i;
  }
}

inline void map_voidp(void* (*fn)(void*), void **array, size_t size_of_array) {
  size_t i = 0;
  while (i < size_of_array) {
    array[i] = fn(array[i]);
    ++i;
  }
}

}
