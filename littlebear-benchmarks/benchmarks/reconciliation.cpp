#include "reconcile.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

// Track C++ heap allocations only during reconciliation, after storage setup.
static size_t allocations = 0;
static bool counting = false;
void *operator new(size_t n) { if (counting) ++allocations; if (void *p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }

namespace benchmark {
namespace r = reconciliation;
struct index { std::vector<uint64_t> ids, prefix, fingerprints; };
uint64_t mix(uint64_t x) {
  x += UINT64_C(0x9e3779b97f4a7c15);
  x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
  return x ^ (x >> 31);
}
void seal(index *s) {
  std::sort(s->ids.begin(), s->ids.end()); s->prefix.push_back(0);
  for (auto id : s->ids) {
    const uint64_t hash = mix(id);
    s->fingerprints.push_back(hash);
    s->prefix.push_back(s->prefix.back() ^ hash);
  }
}
size_t lower(index *s, uint64_t id) { return std::lower_bound(s->ids.begin(), s->ids.end(), id) - s->ids.begin(); }
bool summarize(void *p, r::range range, r::summary *out) {
  auto *s = static_cast<index *>(p);
  const size_t first = lower(s, range.low), end = range.high ? lower(s, range.high) : s->ids.size();
  *out = {end - first, s->prefix[first] ^ s->prefix[end]};
  return true;
}
bool select(void *p, uint64_t rank, uint64_t *out) {
  auto *s = static_cast<index *>(p);
  if (rank >= s->ids.size()) return false;
  *out = s->ids[size_t(rank)];
  return true;
}
size_t list(void *p, r::range range, r::item *out, size_t capacity) {
  auto *s = static_cast<index *>(p); size_t n = 0;
  for (size_t i = lower(s, range.low); i < s->ids.size() && n < capacity && r::contains(range, s->ids[i]); ++i) out[n++] = {s->ids[i], s->fingerprints[i]};
  return n;
}
r::source source(index *s) { return {s, summarize, select, list}; }
// Transport staging is preallocated and excluded from the engine's workspace.
constexpr size_t STAGING = 16 * 1024 * 1024;
struct measurement { uint64_t bytes = 0, checksum = 0; size_t turns = 0, have[2]{}, need[2]{}; };
measurement run(index *a, index *b, uint8_t *input, uint8_t *next) {
  measurement m;
  r::workspace work{};
  r::output out{input, STAGING};
  if (r::begin(source(a), &work, &out) != r::status::complete) std::abort();
  size_t input_size = out.size;
  bool to_b = true;
  uint64_t have[32], need[32];
  uint8_t packet[4096];
  while (input_size > sizeof(r::MAGIC)) {
    ++m.turns; m.bytes += input_size;
    if (m.turns > 100) std::abort();
    size_t next_size = sizeof(r::MAGIC);
    memcpy(next, r::MAGIC, next_size);
    r::cursor at{};
    r::differences differences{have, 32, 0, need, 32, 0};
    for (;;) {
      out = {packet, sizeof(packet)};
      const auto code = r::receive(source(to_b ? b : a), to_b ? r::role::responder : r::role::initiator, {input, input_size}, &at, &work, &out, &differences);
      const size_t side = to_b ? 1 : 0;
      m.have[side] += differences.have_count; m.need[side] += differences.need_count;
      for (size_t i = 0; i < differences.have_count; ++i) m.checksum ^= differences.have[i];
      for (size_t i = 0; i < differences.need_count; ++i) m.checksum ^= differences.need[i];
      if (out.size < sizeof(r::MAGIC)) std::abort();
      const size_t payload = out.size - sizeof(r::MAGIC);
      if (next_size + payload > STAGING) std::abort();
      memcpy(next + next_size, packet + sizeof(r::MAGIC), payload); next_size += payload;
      if (code == r::status::complete) break;
      if (code != r::status::more && code != r::status::output_full && code != r::status::differences_full) std::abort();
    }
    input_size = next_size;
    std::swap(input, next); to_b = !to_b;
  }
  m.bytes += sizeof(r::MAGIC); // final empty acknowledgment
  return m;
}
void scenario(const char *name, size_t shift, size_t replacement, unsigned empty_side) {
  index a, b;
  constexpr size_t N = 16384;
  for (size_t i = 0; i < N; ++i) {
    a.ids.push_back(mix(i));
    if (empty_side != 1) b.ids.push_back(mix(i + shift));
  }
  if (replacement == 1) b.ids.back() = mix(1000000);
  seal(&a); seal(&b);
  if (replacement == 2) {
    b.fingerprints.back() ^= mix(1000000);
    b.prefix.back() ^= mix(1000000);
  }
  if (empty_side == 2) { a.ids.clear(); a.fingerprints.clear(); a.prefix.assign(1, 0); }
  auto *input = static_cast<uint8_t *>(std::malloc(STAGING));
  auto *next = static_cast<uint8_t *>(std::malloc(STAGING));
  if (!input || !next) std::abort();
  uint64_t expected_checksum = 0;
  for (auto id : a.ids) expected_checksum ^= id;
  for (auto id : b.ids) expected_checksum ^= id;
  std::array<double, 9> times{};
  measurement m;
  for (auto &time : times) {
    allocations = 0; counting = true;
    const auto started = std::chrono::steady_clock::now();
    m = run(&a, &b, input, next);
    time = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    counting = false;
    if (allocations || m.have[1] || m.need[1] || m.checksum != expected_checksum) std::abort();
    const size_t expected_have = empty_side == 1 ? N : empty_side == 2 ? 0 : replacement ? 1 : std::min(shift, N);
    const size_t expected_need = empty_side == 1 ? 0 : empty_side == 2 ? N : replacement ? 1 : std::min(shift, N);
    if (m.have[0] != expected_have || m.need[0] != expected_need) std::abort();
  }
  std::sort(times.begin(), times.end());
  std::printf("%s: median_us=%.2f turns=%zu wire_bytes=%llu have=%zu need=%zu engine_allocations=%zu\n", name,
    times[4], m.turns, (unsigned long long)m.bytes, m.have[0], m.need[0], allocations);
  std::free(input); std::free(next);
}
}
#ifndef RECONCILIATION_BENCHMARK_LIBRARY
int main() {
  std::printf("IDs=%zu fingerprints=%zu workspace=%zu min_output=%zu\n", reconciliation::ID_BYTES,
    reconciliation::FINGERPRINT_BYTES, sizeof(reconciliation::workspace), reconciliation::MIN_OUTPUT_CAPACITY);
  benchmark::scenario("equal", 0, 0, false);
  benchmark::scenario("one replacement", 0, 1, false);
  benchmark::scenario("one content edit", 0, 2, false);
  benchmark::scenario("half overlap", 8192, 0, false);
  benchmark::scenario("disjoint", 16384, 0, false);
  benchmark::scenario("empty responder", 0, 0, 1);
  benchmark::scenario("empty initiator", 0, 0, 2);
  std::puts("16,384 records per nonempty peer; in-memory transport and binary-search/prefix-XOR index; excludes storage construction and sockets.");
}
#endif
