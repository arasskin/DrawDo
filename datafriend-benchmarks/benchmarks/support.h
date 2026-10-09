#pragma once
#define XXH_INLINE_ALL
#include "../../third_party/xxhash/xxhash.h"
#include "../../littlebear-benchmarks/benchmarks/reference/tree.h"
#include "../../littlebear-benchmarks/benchmarks/reference/field.h"
#include "datafriend/wide_tree.h"
#include <array>
#include <deque>
#include <set>
#include <memory>

namespace experiment {
using datafriend::record;
using datafriend::key_range;

// Benchmark codec: unsigned integers in big endian; XXH64, seed 0.
inline void put64(uint8_t* p, uint64_t v) {
  for (unsigned i = 0; i < 8; ++i) p[i] = uint8_t(v >> (56-8*i));
}
inline uint64_t key_hash(uint64_t key) {
  uint8_t bytes[8]; put64(bytes, key);
  return XXH64(bytes, sizeof(bytes), 0);
}
inline record make_record(uint64_t key, uint64_t value) {
  uint8_t bytes[16]; put64(bytes, key); put64(bytes+8, value);
  return {key, value, XXH64(bytes, sizeof(bytes), 0)};
}
// Bijective generator, for unique pseudo-random keys without a deduplication table.
inline uint64_t permute(uint64_t x) {
  x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
  return x ^ (x >> 31);
}
inline void require(bool ok, const char* reason) {
  if (!ok) throw std::runtime_error(reason);
}

struct record_index : datafriend::wide_tree {
  record_index() : datafriend::wide_tree(key_hash) {}
  uint64_t range_hash(key_range r) const { return summarize(r).hash; }
};

struct slab_index : datafriend::slab_wide_tree {
  using pool_type=datafriend::slab_pool;
  slab_index() : datafriend::slab_wide_tree(key_hash) {}
  explicit slab_index(pool_type& pool) : datafriend::slab_wide_tree(key_hash,datafriend::slab_nodes(pool)) {}
  uint64_t range_hash(key_range r) const { return summarize(r).hash; }
};

struct binary_pool {
  rank_arena::allocator arena{};
  uint64_t* hashes = nullptr;
  binary_pool() {
    auto result = rank_arena::create();
    require(!result.has_error, "binary arena allocation");
    arena = result.a;
    hashes = hash_field::create();
    if (!hashes) {
      rank_arena::destroy(&arena);
      throw std::runtime_error("binary hash allocation");
    }
  }
  ~binary_pool() {
    rank_arena::destroy(&arena);
    hash_field::destroy(hashes);
  }
  binary_pool(const binary_pool&) = delete;
  size_t reserved_slot_bytes() const {
    size_t n=0;
    for(unsigned i=0;i<32;++i)
      n+=arena.by_rank[i].high_watermark-rank_arena::partition_begin(i);
    return n*24;
  }
};

// Adapter around the EXISTING backend. No server node/count changes.
class binary_index {
  std::unique_ptr<binary_pool> owner_;
  binary_pool* pool_;
 public:
  using pool_type = binary_pool;
  augmented_tree::header tree{};
  uint64_t records=0; // total only; never used for splitting
  bool scan_splits=false;
  explicit binary_index(bool scan=false)
    : owner_(std::make_unique<binary_pool>()),pool_(owner_.get()),scan_splits(scan) {
    tree=augmented_tree::create(&pool_->arena,pool_->hashes);
  }
  explicit binary_index(binary_pool& pool) : pool_(&pool) {
    tree=augmented_tree::create(&pool_->arena,pool_->hashes);
  }
  ~binary_index() {
    if(!owner_) clear_nodes(tree.root);
  }
  binary_index(const binary_index&) = delete;
  binary_index& operator=(const binary_index&) = delete;
  uint64_t size() const { return records; }
  std::optional<record> get(uint64_t key) const {
    auto* t = const_cast<augmented_tree::header*>(&tree);
    const auto leaf = augmented_tree::find(t, key);
    if (leaf == augmented_tree::INVALID_NODE_INDEX) return {};
    return record{key, augmented_tree::node_get_data(t, leaf)[augmented_tree::node_item::v].value, pool_->hashes[leaf]};
  }
  bool set(record r) {
    if (augmented_tree::replace(&tree, r.key, r.value, r.hash)) return false;
    require(augmented_tree::unsafe_associate(&tree, r.key, r.value, r.hash) == augmented_tree::NO_ERRORS,
            "binary insertion capacity");
    ++records; return true;
  }
  bool erase(uint64_t key) {
    auto e = get(key);
    if (!e) return false;
    augmented_tree::unsafe_dissociate(&tree, key, e->hash);
    --records; return true;
  }
  uint64_t fingerprint() const {
    return augmented_tree::get_full_fingerprint(const_cast<augmented_tree::header*>(&tree));
  }
  uint64_t range_hash(key_range r) const {
    if (r.empty() || !records) return 0;
    auto* t = const_cast<augmented_tree::header*>(&tree);
    const auto lo = augmented_tree::get_fingerprint(t, tree.root, r.low);
    const auto hi = r.high ? augmented_tree::get_fingerprint(t, tree.root, *r.high) : fingerprint();
    return lo ^ hi;
  }
  std::vector<record> list(key_range r, size_t limit) const {
    std::vector<record> out;
    out.reserve(std::min<uint64_t>(limit, records));
    if (r.empty() || !records || !limit) return out;
    std::vector<uint32_t> pending{tree.root};
    auto* t = const_cast<augmented_tree::header*>(&tree);
    while (!pending.empty() && out.size() < limit) {
      const auto index = pending.back(); pending.pop_back();
      const auto n = augmented_tree::node_get_data(t, index);
      if (augmented_tree::node_rank(index) == -1) {
        const auto key = n[0].value;
        if (r.contains(key)) out.push_back({key, n[1].value, pool_->hashes[index]});
      } else {
        const auto pivot = n[0].value;
        if (!r.high || pivot < *r.high) pending.push_back(n[1].right_index);
        if (r.low < pivot) pending.push_back(n[1].left_index);
      }
    }
    return out;
  }
  std::vector<uint64_t> cuts(key_range r, size_t fanout) const {
    std::vector<uint64_t> out;
    if (!records || r.empty() || fanout < 2) return out;
    if (scan_splits) {
      // Control: same balanced splitting as the wide tree, paid for by scanning.
      const auto all = list(r, SIZE_MAX);
      fanout = std::min(fanout, all.size());
      for (size_t i = 1; i < fanout; ++i)
        out.push_back(all[(all.size()/fanout)*i + std::min(i, all.size()%fanout)].key);
      return out;
    }
    // Baseline alternative: breadth-first separators, without cached counts.
    std::deque<uint32_t> pending{tree.root};
    auto* t = const_cast<augmented_tree::header*>(&tree);
    while (!pending.empty() && out.size()+1 < fanout) {
      const auto index = pending.front(); pending.pop_front();
      if (augmented_tree::node_rank(index) == -1) continue;
      const auto n = augmented_tree::node_get_data(t, index);
      const auto pivot = n[0].value;
      if (pivot > r.low && (!r.high || pivot < *r.high)) out.push_back(pivot);
      if (r.low < pivot) pending.push_back(n[1].left_index);
      if (!r.high || pivot < *r.high) pending.push_back(n[1].right_index);
    }
    std::sort(out.begin(), out.end());
    return out;
  }
  size_t reserved_slot_bytes() const {
    return pool_->reserved_slot_bytes();
  }
 private:
  void clear_nodes(uint32_t id) {
    if(id==augmented_tree::INVALID_NODE_INDEX) return;
    std::vector<uint32_t> pending{id};
    while(!pending.empty()) {
      const auto i=pending.back();pending.pop_back();
      if(augmented_tree::node_rank(i)!=-1) {
        const auto n=augmented_tree::node_get_data(&tree,i);
        pending.push_back(n[1].left_index);pending.push_back(n[1].right_index);
      }
      rank_arena::release(&pool_->arena,i);
    }
  }
};
} // namespace experiment
