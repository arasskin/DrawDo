#include "main_data_containers/hash_table.h"
#include <cstdio>
#include <ctime>

namespace table_benchmark {
namespace table = hash_table;

uint64_t now() {
  timespec time{};
  if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) std::abort();
  return uint64_t(time.tv_sec) * 1000000000 + time.tv_nsec;
}

uint64_t random(uint64_t *state) {
  *state ^= *state << 13;
  *state ^= *state >> 7;
  *state ^= *state << 17;
  return *state;
}

void report(const char *operation, uint64_t start, size_t count, uint64_t checksum) {
  const uint64_t elapsed = now() - start;
  std::printf("  %s: %.2f ns/op, checksum=%llu\n", operation,
              double(elapsed) / count, (unsigned long long)checksum);
}

bool run(size_t count) {
  auto *keys = (table::key *)std::malloc(count * sizeof(table::key));
  auto t = table::create();
  if (keys == nullptr || table::bad(&t)) {
    std::free(keys);
    table::destroy(&t);
    return false;
  }
  uint64_t state = 0xD0D0;
  for (size_t i = 0; i < count; ++i) keys[i] = {random(&state), random(&state)};
  std::printf("%zu namespaces:\n", count);
  auto started = now();
  for (size_t i = 0; i < count; ++i) {
    table::value value{};
    value.root = uint32_t(i + 1);
    if (!table::unsafe_insert(&t, keys[i], value)) std::abort();
  }
  report("insert including growth", started, count, table::count(&t));

  constexpr size_t QUERIES = 4 * 1024 * 1024;
  uint64_t checksum = 0;
  started = now();
  for (size_t i = 0; i < QUERIES; ++i) {
    const size_t index = (i * 11939) & (count - 1);
    auto *found = table::get(&t, keys[index]);
    if (found == nullptr) std::abort();
    checksum += found->root;
  }
  report("lookup hit", started, QUERIES, checksum);
  if (checksum != (QUERIES / count) * count * (count + 1) / 2) std::abort();

  checksum = 0;
  started = now();
  for (size_t i = 0; i < QUERIES; ++i) {
    auto key = keys[(i * 11939) & (count - 1)];
    key.high64 ^= UINT64_C(0xBADC0FFEE0DDF00D);
    checksum += table::get(&t, key) == nullptr;
  }
  report("lookup miss", started, QUERIES, checksum);
  if (checksum != QUERIES) std::abort();

  started = now();
  for (size_t i = 0; i < count * 32; ++i) {
    const size_t index = (i * 11939) & (count - 1);
    if (!table::dissociate(&t, keys[index])) std::abort();
    table::value value{};
    value.root = uint32_t(index + 1);
    if (!table::unsafe_insert(&t, keys[index], value)) std::abort();
  }
  report("remove/reinsert (one operation each)", started, count * 64, table::count(&t));
  std::printf("  %zu buckets, %zu table bytes, %.2f bytes/namespace, %.1f%% slot occupancy\n",
              t.bucket_count, table::memory_usage(&t), double(table::memory_usage(&t)) / count,
              100.0 * count / (t.bucket_count * table::BUCKET_SIZE));
  table::destroy(&t);
  std::free(keys);
  return true;
}
}

int main() {
  if (!table_benchmark::run(1024) || !table_benchmark::run(16384)) return 1;
  std::puts("Table microbenchmark: uniformly distributed 128-bit keys; excludes tree operations and I/O.");
}
