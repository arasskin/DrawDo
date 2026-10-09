#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>

namespace virtual_mapping {

inline size_t page_size() {
  static const long size = sysconf(_SC_PAGESIZE);
  return size > 0 ? static_cast<size_t>(size) : 0;
}

inline size_t usable_size(size_t bytes) {
  const size_t page = page_size();
  if (page == 0 || bytes == 0 || bytes > SIZE_MAX - 2 * page) return 0;
  return ((bytes + page - 1) / page) * page;
}

inline void *create(size_t bytes) {
  const size_t usable = usable_size(bytes);
  if (usable == 0) return nullptr;
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
  // These are sparse address-space reservations, not a promise to back every
  // slot with RAM/swap. Physical memory is consumed as pages are touched.
  flags |= MAP_NORESERVE;
#endif
  void *start = mmap(nullptr, usable + page_size(), PROT_READ | PROT_WRITE,
                     flags, -1, 0);
  if (start == MAP_FAILED) { perror("virtual arena mmap"); return nullptr; }
  if (mprotect(static_cast<uint8_t *>(start) + usable, page_size(), PROT_NONE) != 0) {
    perror("virtual arena guard page");
    munmap(start, usable + page_size());
    return nullptr;
  }
  return start;
}

inline void destroy(void *start, size_t bytes) {
  if (start != nullptr) munmap(start, usable_size(bytes) + page_size());
}

}
