#include "check.h"
#include "main_data_containers/geometric_rank_partitioned_virtual_arena.h"
#include "main_data_containers/endless_virtual_memory_field.h"
#include <csignal>
#include <sys/wait.h>

namespace arena_tests {
namespace arena = geometric_rank_partioned_virtual_slab_arena;

void guard_fault(int) {
  // Exit directly instead of involving the host's crash/core-dump service.
  _exit(77);
}

void check_guard(void *start, size_t bytes) {
  pid_t child = fork();
  CHECK(child >= 0);
  if (child == 0) {
    signal(SIGSEGV, guard_fault);
    signal(SIGBUS, guard_fault);
    volatile uint8_t *guard = static_cast<uint8_t *>(start) + virtual_mapping::usable_size(bytes);
    *guard = 1;
    _exit(0);
  }
  int status = 0;
  CHECK(waitpid(child, &status, 0) == child);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 77);
}

void ranks_and_reuse() {
  auto [a, error] = arena::create();
  CHECK(!error);
  uint64_t expected_begin = 0;
  for (uint8_t rank = 0; rank < arena::NUMBER_OF_RANKS; ++rank) {
    const uint64_t capacity = uint64_t(1) << (31 - rank);
    const uint32_t end = expected_begin + capacity;
    CHECK(arena::partition_begin(rank) == expected_begin);
    CHECK(arena::partition_end(rank) == end);
    CHECK(arena::partition(expected_begin) == rank);
    CHECK(arena::partition(end - 1) == rank);
    auto first = arena::reserve(&a, rank);
    CHECK(first == expected_begin);
    CHECK(arena::index(&a, arena::address(&a, first)) == first);
    arena::release(&a, first);
    CHECK(arena::reserve(&a, rank) == first);
    if (capacity > 1) {
      // Reach each boundary without committing billions of untouched slots.
      a.by_rank[rank].high_watermark = end - 1;
      CHECK(arena::reserve(&a, rank) == end - 1);
    }
    CHECK(arena::reserve(&a, rank) == arena::INVALID_INDEX);
    auto *last = static_cast<uint64_t *>(arena::address(&a, end - 1));
    last[0] = rank;
    last[1] = UINT64_MAX;
    CHECK(last[0] == rank && last[1] == UINT64_MAX);
    arena::release(&a, end - 1);
    CHECK(arena::reserve(&a, rank) == end - 1);
    CHECK(arena::reserve(&a, rank) == arena::INVALID_INDEX);
    expected_begin = end;
  }
  CHECK(expected_begin == UINT32_MAX);
  CHECK(arena::reserve(&a, 32) == arena::INVALID_INDEX);
  CHECK(arena::reserve(&a, 255) == arena::INVALID_INDEX);
  check_guard(a.start, arena::SIZE);
  arena::destroy(&a);
  CHECK(a.start == nullptr);
  CHECK(arena::reserve(&a, 0) == arena::INVALID_INDEX);
}
}

int main() {
  CHECK(virtual_mapping::page_size() > 0);
  CHECK(virtual_mapping::create(0) == nullptr);
  CHECK(virtual_mapping::create(SIZE_MAX) == nullptr);
  arena_tests::ranks_and_reuse();
  auto *fingerprints = endless_virtual_memory_field::create();
  CHECK(fingerprints != nullptr);
  CHECK(fingerprints[0] == 0 && fingerprints[UINT32_MAX - 1] == 0);
  fingerprints[0] = 42;
  fingerprints[UINT32_MAX - 1] = 99;
  CHECK(fingerprints[0] == 42 && fingerprints[UINT32_MAX - 1] == 99);
  arena_tests::check_guard(fingerprints, endless_virtual_memory_field::SIZE);
  endless_virtual_memory_field::destroy(fingerprints);
  std::puts("PASS: all 32 rank boundaries, exhaustion/reuse, fingerprint array, and guard pages");
}
