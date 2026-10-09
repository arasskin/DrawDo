#include "../benchmarks/support.h"
#include <iostream>
#include <map>
#include <random>

using namespace experiment;

template<class Tree> void verify(const Tree& t, const std::map<uint64_t, record>& model,
            std::mt19937_64& rng) {
  t.validate();
  require(t.size() == model.size(), "size mismatch");
  const auto all = t.list({}, SIZE_MAX);
  std::vector<record> visited;
  t.for_each([&](record r) { visited.push_back(r); });
  require(visited==all,"visitor order/content mismatch");
  require(all.size() == model.size(), "iteration size mismatch");
  uint64_t hash = 0;
  size_t index = 0;
  for (auto [k, e] : model) {
    require(t.get(k) == e, "lookup mismatch");
    require(t.select(index) == e, "select mismatch");
    require(all[index++] == e, "iteration order mismatch");
    hash ^= e.hash;
  }
  require(!t.select(model.size()), "select past end");
  require(hash == t.fingerprint(), "full fingerprint mismatch");
  for (unsigned trial = 0; trial < 10; ++trial) {
    key_range r{rng(), trial % 3 ? std::optional<uint64_t>(rng()) : std::nullopt};
    if (trial == 0) r = {0, UINT64_MAX};
    if (trial == 1) r = {UINT64_MAX, {}};
    std::vector<record> expected;
    hash = 0;
    for (auto it = model.lower_bound(r.low); it != model.end() && r.contains(it->first); ++it) {
      expected.push_back(it->second); hash ^= it->second.hash;
    }
    const auto summary = t.summarize(r);
    require(summary.count == expected.size() && summary.hash == hash, "range summary mismatch");
    require(t.list(r, SIZE_MAX) == expected, "range iteration mismatch");
    expected.resize(std::min<size_t>(7, expected.size()));
    require(t.list(r, 7) == expected, "bounded iteration mismatch");
  }
}

uint64_t zero_hash(uint64_t) { return 0; }
uint64_t rank_zero(uint64_t) { return 1; }

template<class Tree=datafriend::wide_tree> void exercise(typename Tree::key_hash_fn rank_hash, uint64_t seed) {
  Tree tree(rank_hash);
  std::map<uint64_t, record> model;
  std::mt19937_64 rng(seed);
  for (auto k : {uint64_t(0), uint64_t(1), UINT64_MAX}) {
    const auto e = make_record(k, 17);
    tree.set(e); model[k] = e;
  }
  verify(tree, model, rng);
  for (unsigned i = 0; i < 12000; ++i) {
    const auto k = permute(rng()%600);
    if (rng()%3) {
      auto e = make_record(k, rng());
      const bool inserted = !model.contains(k);
      require(tree.set(e) == inserted, "set result");
      model[k] = e;
    } else {
      const bool removed = model.erase(k) != 0;
      require(tree.erase(k) == removed, "erase result");
    }
    if (i%71 == 0) verify(tree, model, rng);
  }
  verify(tree, model, rng);
  // Replacing contents preserves shape; rebuilding in reverse order is canonical.
  const auto before = tree.shape_digest();
  for (auto& [k, e] : model) { e = make_record(k, 99); tree.set(e); }
  require(before == tree.shape_digest(), "replacement changed shape");
  Tree rebuilt(rank_hash);
  for (auto it = model.rbegin(); it != model.rend(); ++it) rebuilt.set(it->second);
  require(rebuilt.shape_digest() == tree.shape_digest(), "history-dependent shape");
  verify(rebuilt, model, rng);
  std::vector<uint64_t> keys;
  for (auto [k, _] : model) keys.push_back(k);
  std::shuffle(keys.begin(), keys.end(), rng);
  for (auto k : keys) {
    require(tree.erase(k), "clear deletion"); model.erase(k);
    if (model.size()%43 == 0) verify(tree, model, rng);
  }
  require(tree.allocations().live_bytes == 0 && tree.allocations().live_nodes == 0,
          "allocations retained after clear");
  verify(tree, model, rng);
}

int main() {
  try {
    exercise(key_hash, 1); exercise(key_hash, 90);
    exercise(zero_hash, 71); exercise(rank_zero, 333);
    exercise<datafriend::slab_wide_tree>(key_hash,1);
    exercise<datafriend::slab_wide_tree>(key_hash,90);
    exercise<datafriend::slab_wide_tree>(zero_hash,71);
    exercise<datafriend::slab_wide_tree>(rank_zero,333);
    std::cout << "PASS: randomized model, ranges, rank/select, history independence, extreme ranks, reclamation\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
  }
}
