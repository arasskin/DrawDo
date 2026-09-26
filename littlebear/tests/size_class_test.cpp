#include "check.h"
#include "size_class_file_heap.h"
#include <algorithm>
#include <map>
#include <random>
#include <vector>

namespace heap_tests {
namespace heap = size_class_file_heap;

void sizes() {
  // Independent, explicit sequence of allowed physical sizes.
  std::vector<uint32_t> slots{32};
  for (uint32_t base = 32; base < 65536; base *= 2)
    for (uint32_t size = base + base / 8; size <= base * 2; size += base / 8)
      slots.push_back(size);
  for (uint32_t bytes = 1; bytes <= UINT16_MAX; ++bytes) {
    auto expected = *std::lower_bound(slots.begin(), slots.end(), bytes);
    auto actual = size_class_file_heap::next_smallest_slot_size(bytes);
    if (actual != expected)
      std::fprintf(stderr, "request=%u expected=%u actual=%u\n", bytes, expected, unsigned(actual));
    CHECK(actual == expected);
    const auto index = heap::size_class_index(bytes);
    CHECK(index == std::lower_bound(slots.begin(), slots.end(), bytes) - slots.begin());
    CHECK(heap::slot_size(index) == expected);
  }
  CHECK(slots.size() == heap::NUMBER_OF_SIZE_CLASSES);
  for (size_t invalid : {size_t(0), size_t(65536), SIZE_MAX}) {
    CHECK(heap::size_class_index(invalid) == heap::INVALID_SIZE_CLASS);
    CHECK(heap::next_smallest_slot_size(invalid) == 0);
  }
  CHECK(heap::slot_size(heap::INVALID_SIZE_CLASS) == 0);
  CHECK(heap::slot_size(heap::NUMBER_OF_SIZE_CLASSES) == 0);
  std::puts("PASS: all 65,535 supported request sizes");
}

heap::allocator create(uint64_t limit, int metadata_bytes = 32) {
  heap::allocator a{};
  a.limit = limit;
  a.slab_allocator = growing_slab_arena::create(sizeof(heap::slab), metadata_bytes);
  CHECK(!growing_slab_arena::bad(&a.slab_allocator));
  return a;
}

void every_size_reuses_class() {
  size_t first_size = 1;
  for (uint8_t c = 0; c < heap::NUMBER_OF_SIZE_CLASSES; ++c) {
    const uint32_t physical = heap::slot_size(c);
    auto a = create(physical);
    for (size_t size = first_size; size <= std::min(size_t(physical), heap::MAX_VALUE_SIZE); ++size) {
      auto allocation = heap::reserve(&a, size);
      CHECK(!allocation.has_error && allocation.offset == 0);
      CHECK(a.high_watermark == physical);
      CHECK(heap::reserve(&a, size).has_error);
      CHECK(heap::release(&a, allocation.offset, size));
    }
    first_size = physical + 1;
    growing_slab_arena::destroy(&a.slab_allocator);
  }
}

void capacity_and_reuse() {
  auto a = create(32);
  CHECK(heap::reserve(&a, 0).has_error);
  CHECK(heap::reserve(&a, 65536).has_error);
  CHECK(heap::reserve(&a, SIZE_MAX).has_error);
  CHECK(a.high_watermark == 0);
  auto first = heap::reserve(&a, 32);
  CHECK(!first.has_error && first.offset == 0);
  CHECK(heap::reserve(&a, 1).has_error);
  CHECK(heap::release(&a, first.offset, 32));
  CHECK(heap::reserve(&a, 33).has_error); // another class cannot consume this slot
  auto reused = heap::reserve(&a, 1);
  CHECK(!reused.has_error && reused.offset == 0);
  CHECK(!heap::release(&a, 0, 0));
  CHECK(!heap::release(&a, 0, 65536));
  CHECK(!heap::release(&a, 32, 32));
  growing_slab_arena::destroy(&a.slab_allocator);

  a = create(65535);
  CHECK(heap::reserve(&a, 65535).has_error);
  a.limit = 65536;
  CHECK(!heap::reserve(&a, 65535).has_error);
  CHECK(a.high_watermark == 65536);
  CHECK(heap::release(&a, 0, 65535));
  CHECK(!heap::reserve(&a, 65535).has_error);
  a.high_watermark = UINT64_MAX - 15;
  a.limit = UINT64_MAX;
  CHECK(heap::reserve(&a, 32).has_error); // addition must not wrap
  growing_slab_arena::destroy(&a.slab_allocator);
}

void randomized_reuse() {
  auto a = create(16 * 1024 * 1024);
  std::mt19937_64 random(0xD0D0);
  std::vector<std::pair<uint64_t, size_t>> live;
  std::map<uint64_t, uint32_t> intervals;
  for (size_t step = 0; step < 30000; ++step) {
    if (!live.empty() && (live.size() >= 512 || random() % 2 == 0)) {
      const size_t i = random() % live.size();
      CHECK(heap::release(&a, live[i].first, live[i].second));
      CHECK(intervals.erase(live[i].first) == 1);
      live[i] = live.back();
      live.pop_back();
    } else {
      const size_t size = 1 + random() % 65535;
      const auto allocation = heap::reserve(&a, size);
      if (allocation.has_error) continue;
      const uint32_t physical = heap::next_smallest_slot_size(size);
      auto next = intervals.lower_bound(allocation.offset);
      if (next != intervals.end()) CHECK(allocation.offset + physical <= next->first);
      if (next != intervals.begin()) {
        auto previous = std::prev(next);
        CHECK(previous->first + previous->second <= allocation.offset);
      }
      CHECK(intervals.emplace(allocation.offset, physical).second);
      live.emplace_back(allocation.offset, size);
    }
  }
  for (auto [offset, size] : live) CHECK(heap::release(&a, offset, size));
  // Force metadata growth and verify all saved offsets survive relocation.
  std::vector<uint64_t> offsets;
  for (size_t i = 0; i < 4096; ++i) {
    auto allocation = heap::reserve(&a, 32);
    CHECK(!allocation.has_error);
    offsets.push_back(allocation.offset);
  }
  for (auto offset : offsets) CHECK(heap::release(&a, offset, 32));
  const auto watermark = a.high_watermark;
  for (auto i = offsets.rbegin(); i != offsets.rend(); ++i) {
    auto allocation = heap::reserve(&a, 32);
    CHECK(!allocation.has_error && allocation.offset == *i);
  }
  CHECK(a.high_watermark == watermark);
  growing_slab_arena::destroy(&a.slab_allocator);
}

void metadata_growth_and_failure() {
  namespace arena = growing_slab_arena;
  auto a = arena::create(16, 32);
  CHECK(arena::reserve(&a) == 0);
  CHECK(arena::reserve(&a) == 1);
  arena::release(&a, 0);
  arena::release(&a, 1);
  CHECK(arena::grow(&a));
  CHECK(arena::reserve(&a) == 1);
  CHECK(arena::reserve(&a) == 0);
  CHECK(arena::reserve(&a) == 2);
  a.size = INT_MAX;
  a.high_watermark_index = INT_MAX;
  CHECK(!arena::grow(&a));
  CHECK(a.size == INT_MAX);
  CHECK(arena::reserve(&a) == -1);
  arena::destroy(&a);
  CHECK(arena::reserve(&a) == -1);
  a = arena::create(0, 32);
  CHECK(arena::bad(&a));

  auto h = create(32);
  CHECK(!heap::reserve(&h, 32).has_error);
  const int capacity = h.slab_allocator.size;
  h.slab_allocator.size = INT_MAX;
  h.slab_allocator.high_watermark_index = INT_MAX;
  CHECK(!heap::release(&h, 0, 32));
  CHECK(h.size_class_heads[0].next_slab_index == -1);
  CHECK(heap::reserve(&h, 32).has_error);
  h.slab_allocator.size = capacity;
  h.slab_allocator.high_watermark_index = 0;
  CHECK(heap::release(&h, 0, 32));
  CHECK(!heap::reserve(&h, 32).has_error);
  arena::destroy(&h.slab_allocator);
}

void file_lifetime() {
  char name[] = "/tmp/littlebear-heap-test-XXXXXX";
  int fd = mkstemp(name);
  CHECK(fd >= 0);
  close(fd);
  auto file = heap::create(name, 1024);
  unlink(name);
  CHECK(!file.has_error);
  CHECK(fcntl(file.file_descriptor, F_GETFD) & FD_CLOEXEC);
  CHECK(!heap::reserve(&file.file_allocator, 1).has_error);
  heap::destroy(&file);
  CHECK(file.file_descriptor == -1);
  CHECK(file.file_allocator.slab_allocator.start == nullptr);
}
}

int main() {
  heap_tests::sizes();
  heap_tests::every_size_reuses_class();
  heap_tests::capacity_and_reuse();
  heap_tests::randomized_reuse();
  heap_tests::metadata_growth_and_failure();
  heap_tests::file_lifetime();
  std::puts("PASS: heap capacity, reuse, metadata growth/failure, and file lifetime");
}
