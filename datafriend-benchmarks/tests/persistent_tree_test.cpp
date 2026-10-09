#include "datafriend/persistent_wide_tree.h"
#include <iostream>
#include <map>
#include <random>
#include <thread>
#include <array>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct accounting {
  std::atomic<size_t> live_bytes{0}, live_nodes{0}, calls{0}, fail_at{0};
};
struct tracked_allocator {
  std::shared_ptr<accounting> state;
  void* allocate(size_t bytes) {
    const auto call = state->calls.fetch_add(1) + 1;
    if (call == state->fail_at.load()) return nullptr;
    void* p = std::malloc(bytes);
    if (p) { state->live_bytes.fetch_add(bytes); state->live_nodes.fetch_add(1); }
    return p;
  }
  void deallocate(void* p, size_t bytes) noexcept {
    state->live_bytes.fetch_sub(bytes); state->live_nodes.fetch_sub(1); std::free(p);
  }
};
using tree = datafriend::basic_persistent_wide_tree<uint64_t, tracked_allocator>;
using model = std::map<uint64_t, uint64_t>;
void verify(const tree& t, const model& m) {
  t.validate();
  check(t.size() == m.size(), "size");
  auto all = t.list();
  check(all.size() == m.size(), "list size");
  uint64_t hash = 0;
  size_t i = 0;
  for (auto [k, v] : m) {
    tree::entry e{k, v, v};
    check(t.get(k) == e && t.select(i) == e && all[i] == e, "record/order/select");
    hash ^= v; ++i;
  }
  check(t.fingerprint() == hash && !t.select(i), "fingerprint/select end");
  std::mt19937_64 random(12);
  for (size_t j = 0; j < 12; ++j) {
    tree::range r{random(), j % 3 ? std::optional<uint64_t>(random()) : std::nullopt};
    if (!j) r = {};
    if (j == 1) r = {UINT64_MAX, {}};
    if (j == 2) r = {0, UINT64_MAX};
    if (j == 3) r = {99, 99};
    std::vector<tree::entry> expected;
    hash = 0;
    for (auto it = m.lower_bound(r.low); it != m.end() && r.contains(it->first); ++it) {
      expected.push_back({it->first, it->second, it->second}); hash ^= it->second;
    }
    auto summary = t.summarize(r);
    check(summary.count == expected.size() && summary.hash == hash, "range summary");
    check(t.list(r) == expected, "range list");
    for (size_t fanout : {size_t(0), size_t(1), size_t(2), size_t(16), size_t(1000)}) {
      auto cuts = t.cuts(r, fanout);
      const auto f = std::min(fanout, expected.size());
      check(cuts.size() == (f < 2 ? 0 : f - 1), "cut count");
      for (size_t k = 1; k < f; ++k) {
        const auto offset = (expected.size()/f)*k + std::min(k, expected.size()%f);
        check(cuts[k-1] == expected[offset].key, "balanced cut");
      }
    }
    expected.resize(std::min<size_t>(7, expected.size()));
    check(t.list(r, 7) == expected && t.list(r, 0).empty(), "bounded list");
  }
}
void histories(unsigned mode) {
  auto stats = std::make_shared<accounting>();
  {
    tree t(tracked_allocator{stats});
    model m;
    std::vector<std::pair<tree, model>> versions;
    std::mt19937_64 random(92 + mode);
    std::vector<uint64_t> keys{0, UINT64_MAX};
    for (uint64_t i = 1; i < 180; ++i)
      keys.push_back(mode == 1 ? (i << 32) : mode == 2 ? ((i << 4) | 1) : random());
    for (size_t i = 0; i < 3000; ++i) {
      auto key = keys[random()%keys.size()];
      auto old = t;
      auto old_model = m;
      const auto allocated = stats->calls.load();
      auto copy = t;
      check(copy.shares_root_with(t) && stats->calls == allocated, "copy allocated nodes");
      if (random()%3) {
        const uint64_t value = random();
        t = t.set(key, value, value); m[key] = value;
      } else { t = t.remove(key); m.erase(key); }
      if (i % 97 == 0) { verify(t, m); verify(old, old_model); }
      if (i % 211 == 0) versions.emplace_back(t, m);
      if (i % 313 == 0) {
        for (const auto& [v, expected] : versions) verify(v, expected);
        // Branch an old version independently, without changing either lineage.
        auto& [base, expected] = versions.front();
        auto branch = base.set(0x1111111111111111, 22, 22);
        auto branch_model = expected; branch_model[0x1111111111111111] = 22;
        verify(branch, branch_model); verify(base, expected);
      }
    }
    verify(t, m);
    tree rebuilt(tracked_allocator{stats});
    for (auto it = m.rbegin(); it != m.rend(); ++it) rebuilt = rebuilt.set(it->first, it->second, it->second);
    check(t.shape_digest() == rebuilt.shape_digest(), "history-dependent structure");
    for (auto key : keys) { t = t.remove(key); m.erase(key); }
    verify(t, m);
    for (const auto& [v, expected] : versions) verify(v, expected);
    // Empty values and a zero XOR never mean an absent node.
    t = t.set(0, 1, 1); check(t.size() == 1 && t.fingerprint() == 1, "zero key");
    t = t.set(1, 2, 2).set(2, 3, 3).set(3, 4, 4);
    check(t.size() == 4 && t.fingerprint() == (1^2^3^4), "aggregate");
    auto moved = std::move(t); check(t.empty() && moved.size() == 4, "move constructor");
    t = t.set(7, 8, 8); check(t.size() == 1, "moved-from reuse");
    t = std::move(moved); check(moved.empty() && t.size() == 4, "move assignment");
    auto& self = t; t = self; check(t.size() == 4, "self copy");
  }
  check(stats->live_nodes == 0 && stats->live_bytes == 0, "history leak");
}
void failure_paths() {
  auto stats = std::make_shared<accounting>();
  size_t insertion_failures = 0, removal_failures = 0;
  {
    tree base(tracked_allocator{stats});
    model m;
    std::mt19937_64 random(971);
    for (size_t i = 0; i < 512; ++i) {
      const auto k = random(); base = base.set(k, i, i); m[k] = i;
    }
    auto original = base;
    auto shape = base.shape_digest();
    std::vector<uint64_t> edits{0, UINT64_MAX, uint64_t(1)<<32, 16, 1};
    for (size_t i = 0; i < 20; ++i) edits.push_back(random());
    auto run = [&](uint64_t key, bool removing) {
      for (size_t fail = 1; fail < 100; ++fail) {
        const auto bytes = stats->live_bytes.load(), nodes = stats->live_nodes.load();
        stats->fail_at = stats->calls.load() + fail;
        bool succeeded = false;
        try {
          auto changed = removing ? base.remove(key) : base.set(key, 42, 42);
          auto expected = m;
          if (removing) expected.erase(key); else expected[key] = 42;
          verify(changed, expected); succeeded = true;
        } catch (const std::bad_alloc&) {
          if (removing) ++removal_failures; else ++insertion_failures;
        }
        stats->fail_at = 0;
        check(stats->live_bytes == bytes && stats->live_nodes == nodes, "failed edit leaked nodes");
        check(base.shares_root_with(original) && base.shape_digest() == shape, "failed edit mutated source");
        verify(base, m);
        if (succeeded) return;
      }
      throw std::runtime_error("failure sweep never succeeded");
    };
    for (auto key : edits) run(key, false);
    size_t i = 0;
    for (auto [key, value] : m) { if (i++ == 30) break; run(key, true); }
    // Include the highest-rank separator so deletion exercises long joins and
    // collapsing a promoted root, not just ordinary leaf deletions.
    base = base.set(uint64_t(1)<<32, 42, 42); m[uint64_t(1)<<32] = 42;
    original = base;
    shape = base.shape_digest(); run(uint64_t(1)<<32, true);
    run(m.begin()->first, false); // replacement allocation failures
  }
  check(insertion_failures > 30 && removal_failures > 30, "insufficient failure coverage");
  check(stats->live_nodes == 0 && stats->live_bytes == 0, "failure sweep leak");
  std::cout << "Persistent allocation failures checked: insert=" << insertion_failures
            << " remove=" << removal_failures << '\n';
}
void sharing_and_collision() {
  auto stats = std::make_shared<accounting>();
  {
    tree base(tracked_allocator{stats});
    std::mt19937_64 random(33);
    for (size_t i = 0; i < 10000; ++i) base = base.set(random(), i, i);
    auto before = stats->live_nodes.load(), calls = stats->calls.load();
    auto changed = base.set(7, 4, 4);
    check(stats->live_nodes - before < 30 && stats->calls - calls < 30, "edit copied the whole tree");
    auto e = base.select(32).value();
    calls = stats->calls;
    auto duplicate = base.set(e.key, e.value, e.value);
    auto absent = base.remove(7);
    check(duplicate.shares_root_with(base) && absent.shares_root_with(base) && stats->calls == calls, "no-op edit allocated");
    auto replaced = base.set(e.key, e.value + 1, e.value + 1);
    check(replaced.size() == base.size() && replaced.shape_digest() == base.shape_digest(), "replacement changed shape");
    check(base.get(e.key) == e && replaced.get(e.key)->value == e.value + 1, "replacement mutated original");
    check(replaced.fingerprint() == (base.fingerprint() ^ e.hash ^ (e.value + 1)), "replacement aggregate");
  }
  check(stats->live_nodes == 0, "sharing leak");
  datafriend::immutable_bytes payload;
  std::optional<datafriend::persistent_wide_tree::entry> retained;
  {
    std::string input("a\0b", 3);
    payload = datafriend::immutable_bytes(input);
    input[0] = 'z';
    datafriend::persistent_wide_tree map;
    map = map.set(1, payload, 1).set(2, datafriend::immutable_bytes{}, 2);
    auto copied = map.set(3, datafriend::immutable_bytes("later"), 3);
    retained = copied.get(1);
    check(retained->value.shares_storage_with(payload), "payload copied during edit");
    check(retained->value.bytes() == std::string_view("a\0b",3), "mutable input alias");
    check(map.set(2, datafriend::immutable_bytes(""), 2).shares_root_with(map), "empty equality");
  }
  check(retained->value.bytes() == std::string_view("a\0b",3), "payload outlived map incorrectly");
}
void thread_lifetime() {
  auto stats = std::make_shared<accounting>();
  std::weak_ptr<accounting> lifetime = stats;
  std::array<std::thread,4> threads;
  std::atomic<bool> go = false, ok = true;
  {
    tree base(tracked_allocator{stats});
    for (uint64_t i = 1; i <= 1000; ++i) base = base.set(i, i, i);
    for (size_t id = 0; id < threads.size(); ++id) {
      threads[id] = std::thread([base, id, &go, &ok]() mutable {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        try {
          auto current = base;
          for (uint64_t i = 0; i < 500; ++i) {
            const auto k = 10000 + 1000*id + i;
            auto next = current.set(k, k, k);
            check(base.size() == 1000 && !base.get(k), "cross-thread mutation");
            current = next.remove(k);
            check(current.size() == 1000, "thread edit size");
          }
          current.validate();
        } catch (...) { ok = false; }
      });
    }
  } // original root and allocator owner die before threads begin editing
  go.store(true, std::memory_order_release);
  for (auto& thread : threads) thread.join();
  check(ok && stats->live_nodes == 0 && stats->live_bytes == 0, "thread reclamation");
  stats.reset(); check(lifetime.expired(), "allocator lifetime leak");
}
}
int main() {
  try {
    histories(0); histories(1); histories(2);
    failure_paths(); sharing_and_collision(); thread_lifetime();
    std::cout << "PASS: persistent histories, 64-bit keys, count/XOR, ordered ranges, rank/select, sharing, stable-key replacements, failure rollback, thread lifetime\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
