#include "size_class_file_heap.h"
#include <ctime>
#include <cstdlib>

namespace allocator_benchmark {
namespace heap = size_class_file_heap;
constexpr size_t INPUT_COUNT = 4096;
constexpr size_t ROUNDING_OPERATIONS = 16 * 1024 * 1024;
constexpr size_t HEAP_ROUNDS = 512;

uint64_t nanoseconds() {
  timespec now{};
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) std::abort();
  return uint64_t(now.tv_sec) * 1000000000 + now.tv_nsec;
}

uint64_t next_random(uint64_t *state) {
  *state ^= *state << 13;
  *state ^= *state >> 7;
  *state ^= *state << 17;
  return *state;
}

uint32_t reference_rounding(const uint32_t *classes, uint16_t size) {
  size_t low = 0;
  size_t high = heap::NUMBER_OF_SIZE_CLASSES;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (classes[middle] < size) low = middle + 1;
    else high = middle;
  }
  return classes[low];
}

void report(const char *name, uint64_t started, size_t count, uint64_t checksum) {
  const uint64_t duration = nanoseconds() - started;
  std::printf("%s: %.2f ns/op, %.2f million ops/s, checksum=%llu\n", name,
              double(duration) / count, double(count) * 1000 / duration,
              (unsigned long long)checksum);
}
}

int main() {
  using namespace allocator_benchmark;
  uint16_t sizes[INPUT_COUNT];
  uint64_t offsets[INPUT_COUNT];
  uint32_t classes[heap::NUMBER_OF_SIZE_CLASSES];
  uint64_t random = 0xD0D0;
  uint64_t capacity = 0;
  for (size_t i = 0; i < INPUT_COUNT; ++i) {
    sizes[i] = 1 + next_random(&random) % UINT16_MAX;
    capacity += heap::next_smallest_slot_size(sizes[i]);
  }
  for (size_t i = 0; i < heap::NUMBER_OF_SIZE_CLASSES; ++i) classes[i] = heap::slot_size(i);

  uint64_t sum = 0;
  auto started = nanoseconds();
  for (size_t i = 0; i < ROUNDING_OPERATIONS; ++i)
    sum += heap::next_smallest_slot_size(sizes[i % INPUT_COUNT]);
  report("arithmetic size-class rounding", started, ROUNDING_OPERATIONS, sum);
  const uint64_t arithmetic_sum = sum;

  sum = 0;
  started = nanoseconds();
  for (size_t i = 0; i < ROUNDING_OPERATIONS; ++i)
    sum += reference_rounding(classes, sizes[i % INPUT_COUNT]);
  report("reference binary-search rounding", started, ROUNDING_OPERATIONS, sum);
  if (sum != arithmetic_sum) return 1;

  heap::allocator a{};
  a.limit = capacity;
  a.slab_allocator = growing_slab_arena::create(sizeof(heap::slab), INPUT_COUNT * sizeof(heap::slab));
  if (growing_slab_arena::bad(&a.slab_allocator)) return 1;
  sum = 0;
  started = nanoseconds();
  for (size_t round = 0; round < HEAP_ROUNDS; ++round) {
    for (size_t i = 0; i < INPUT_COUNT; ++i) {
      auto allocation = heap::reserve(&a, sizes[i]);
      if (allocation.has_error) return 1;
      offsets[i] = allocation.offset;
      sum += allocation.offset;
    }
    for (size_t i = INPUT_COUNT; i > 0; --i)
      if (!heap::release(&a, offsets[i - 1], sizes[i - 1])) return 1;
  }
  report("reserve/release (one operation each)", started, HEAP_ROUNDS * INPUT_COUNT * 2, sum);
  std::printf("heap metadata: %zu bytes for %zu reusable slots; logical file extent: %llu bytes\n",
              sizeof(a) + a.slab_allocator.size * a.slab_allocator.slab_size, INPUT_COUNT,
              (unsigned long long)a.high_watermark);
  growing_slab_arena::destroy(&a.slab_allocator);
  std::puts("Microbenchmark only: file offsets and metadata, no record I/O or end-to-end sync.");
}
