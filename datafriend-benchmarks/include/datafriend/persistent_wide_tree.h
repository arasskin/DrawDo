// Persistent adaptation of ClojureDart's HashRankedWideTreapMap algorithm.
// Original copyright (c) Baptiste Dupuch & Christophe Grand. All rights reserved.
// Eclipse Public License 1.0; see ../../licenses/epl-v10.html and ../../NOTICE.md.
// This notice must not be removed. C++ adaptation and augmentation: DrawDo.
#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include "node_allocator.h"

namespace datafriend {

// Immutable, owning bytes. Construction copies input; copies share the payload.
// No mutable alias to the allocation is exposed, including for empty values.
class immutable_bytes {
  std::shared_ptr<const std::string> bytes_;
 public:
  immutable_bytes() = default;
  explicit immutable_bytes(std::string_view bytes)
      : bytes_(std::make_shared<const std::string>(bytes)) {}
  std::string_view bytes() const noexcept { return bytes_ ? std::string_view(*bytes_) : std::string_view{}; }
  bool operator==(const immutable_bytes& other) const noexcept {
    return bytes_ == other.bytes_ || bytes() == other.bytes();
  }
  bool shares_storage_with(const immutable_bytes& other) const noexcept { return bytes_ == other.bytes_; }
};

class fingerprint_collision : public std::runtime_error {
 public:
  const uint64_t key;
  explicit fingerprint_collision(uint64_t k)
      : std::runtime_error("different payloads have the same fingerprint key"), key(k) {}
};

// Persistent ordered map: stable uint64 keys and separate content fingerprints.
// Node XORs combine content fingerprints. set/remove return new values.
// Published nodes are never mutated. Distinct map values can be read/edited/
// released on different threads after normal synchronized publication. Concurrent
// assignment/destruction of the same C++ map object still requires synchronization.
// Custom Value must be immutable; Allocator must support concurrent allocate/free
// and release on another thread. The existing single-thread slab pool does not.
// Allocation policies are shared by all descendants and outlive their nodes.
template<class Value = immutable_bytes, class Allocator = malloc_nodes>
class basic_persistent_wide_tree {
 public:
  struct entry {
    uint64_t key;
    Value value;
    uint64_t hash;
    bool operator==(const entry&) const = default;
  };
  struct range {
    uint64_t low = 0;
    std::optional<uint64_t> high; // exclusive; null means infinity
    bool empty() const noexcept { return high && low >= *high; }
    bool contains(uint64_t k) const noexcept { return k >= low && (!high || k < *high); }
  };
  struct summary { uint64_t count = 0, hash = 0; };
 private:
  static_assert(std::is_nothrow_copy_constructible_v<Value> && std::is_nothrow_destructible_v<Value>);
  struct node {
    std::atomic<uint64_t> references{1};
    uint64_t count = 0, hash = 0;
    uint32_t size = 0, rank = 0; // entries in a leaf; separators in an internal node
  };
  static_assert(sizeof(void*) == 8, "Datafriend requires a 64-bit target");
  static_assert(sizeof(node) == 32 && alignof(entry) <= alignof(node));
  struct storage {
    Allocator allocator;
    explicit storage(Allocator a) : allocator(std::move(a)) {}
  };
  std::shared_ptr<storage> storage_;
  node* root_ = nullptr;

  static entry* entries(node* n) { return reinterpret_cast<entry*>(n + 1); }
  static const entry* entries(const node* n) { return reinterpret_cast<const entry*>(n + 1); }
  static node** children(node* n) { return reinterpret_cast<node**>(n + 1); }
  static node* const* children(const node* n) { return reinterpret_cast<node* const*>(n + 1); }
  static uint64_t* keys(node* n) { return reinterpret_cast<uint64_t*>(children(n) + size_t(n->size) + 1); }
  static const uint64_t* keys(const node* n) { return reinterpret_cast<const uint64_t*>(children(n) + size_t(n->size) + 1); }
  static uint64_t count(const node* n) { return n ? n->count : 0; }
  static uint64_t hash(const node* n) { return n ? n->hash : 0; }
  static unsigned rank(uint64_t k) {
    // Stafford Mix13 with a fixed offset: both halves of packed EA affect rank.
    // Ordering remains the original key; equal ranks share variable-size nodes.
    k += 0x9e3779b97f4a7c15ULL;
    k = (k ^ (k >> 30)) * 0xbf58476d1ce4e5b9ULL;
    k = (k ^ (k >> 27)) * 0x94d049bb133111ebULL;
    k ^= k >> 31;
    return std::countr_zero(uint32_t(k)) / 4;
  }
  static size_t allocation_size(size_t size, unsigned depth) {
    const size_t unit = depth ? sizeof(uint64_t) + sizeof(node*) : sizeof(entry);
    const size_t base = sizeof(node) + (depth ? sizeof(node*) : 0);
    if (size > UINT32_MAX || size > (SIZE_MAX - base) / unit) throw std::bad_alloc{};
    return base + size * unit;
  }
  static void retain(node* n) noexcept {
    if (n) {
      const auto old = n->references.fetch_add(1, std::memory_order_relaxed);
      // A reference overflow cannot be allowed to turn a live node into a free one.
      if (old == UINT64_MAX) std::terminate();
    }
  }
  static void release(storage* s, node* n) noexcept {
    if (!n || n->references.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
    if (n->rank) {
      for (size_t i = 0; i <= n->size; ++i) release(s, children(n)[i]);
    } else {
      for (size_t i = 0; i < n->size; ++i) std::destroy_at(entries(n) + i);
    }
    const auto bytes = allocation_size(n->size, n->rank);
    std::destroy_at(n);
    s->allocator.deallocate(n, bytes);
  }
  // Guards own one reference during construction, so an exception at any later
  // allocation unwinds the new paths without touching the source map.
  struct owned_node {
    storage* owner = nullptr;
    node* p = nullptr;
    owned_node() = default;
    owned_node(storage* s, node* n) : owner(s), p(n) {}
    owned_node(const owned_node&) = delete;
    owned_node& operator=(const owned_node&) = delete;
    owned_node(owned_node&& other) noexcept : owner(other.owner), p(std::exchange(other.p, nullptr)) {}
    owned_node& operator=(owned_node&& other) noexcept {
      if (this != &other) { release(owner, p); owner = other.owner; p = std::exchange(other.p, nullptr); }
      return *this;
    }
    ~owned_node() { release(owner, p); }
    node* take() noexcept { return std::exchange(p, nullptr); }
  };
  owned_node hold(node* n) const noexcept { retain(n); return {storage_.get(), n}; }
  node* allocate(size_t size, unsigned depth) const {
    void* p = storage_->allocator.allocate(allocation_size(size, depth));
    if (!p) throw std::bad_alloc{};
    auto* n = new(p) node;
    n->size = uint32_t(size); n->rank = depth;
    return n;
  }
  template<class EntryAt> owned_node leaf(size_t size, EntryAt at) const {
    if (!size) return {};
    node* n = allocate(size, 0);
    // Internal accessors and Value copies cannot throw. No partially initialized
    // node is ever published or passed to release().
    for (size_t i = 0; i < size; ++i) {
      std::construct_at(entries(n) + i, at(i));
      n->hash ^= entries(n)[i].hash;
    }
    n->count = size;
    return {storage_.get(), n};
  }
  template<class KeyAt, class ChildAt>
  owned_node internal(unsigned depth, size_t size, KeyAt key_at, ChildAt child_at) const {
    assert(depth);
    node* n = allocate(size, depth);
    for (size_t i = 0; i < size; ++i) std::construct_at(keys(n) + i, key_at(i));
    for (size_t i = 0; i <= size; ++i) {
      node* child = child_at(i);
      std::construct_at(children(n) + i, child);
      retain(child);
      n->count += count(child); n->hash ^= hash(child);
    }
    owned_node result{storage_.get(), n};
    if (!n->count) return {}; // result releases an empty internal path
    return result;
  }
  static size_t lower(const node* n, uint64_t k) {
    size_t lo = 0, hi = n ? n->size : 0;
    while (lo < hi) {
      const auto mid = lo + (hi - lo) / 2;
      if (entries(n)[mid].key < k) lo = mid + 1; else hi = mid;
    }
    return lo;
  }
  static size_t branch(const node* n, uint64_t k) {
    size_t lo = 0, hi = n ? n->size : 0;
    while (lo < hi) {
      const auto mid = lo + (hi - lo) / 2;
      if (keys(n)[mid] <= k) lo = mid + 1; else hi = mid;
    }
    return lo;
  }
  owned_node with_child(const node* n, unsigned depth, size_t i, node* child) const {
    return internal(depth, n ? n->size : 0,
      [&](size_t j) { return keys(n)[j]; },
      [&](size_t j) { return j == i ? child : children(n)[j]; });
  }
  std::pair<owned_node, owned_node> split_insert(node* n, unsigned depth, const entry& e) const {
    if (!depth) {
      const size_t i = lower(n, e.key), size = n ? n->size : 0;
      auto left = i == size ? hold(n) : leaf(i, [&](size_t j) { return entries(n)[j]; });
      auto right = leaf(size - i + 1, [&](size_t j) { return j ? entries(n)[i + j - 1] : e; });
      return {std::move(left), std::move(right)};
    }
    const size_t i = branch(n, e.key), size = n ? n->size : 0;
    auto [left_child, right_child] = split_insert(n ? children(n)[i] : nullptr, depth - 1, e);
    auto left = internal(depth, i,
      [&](size_t j) { return keys(n)[j]; },
      [&](size_t j) { return j == i ? left_child.p : children(n)[j]; });
    auto right = internal(depth, size - i,
      [&](size_t j) { return keys(n)[i + j]; },
      [&](size_t j) { return j ? children(n)[i + j] : right_child.p; });
    return {std::move(left), std::move(right)};
  }
  owned_node insert(node* n, unsigned depth, unsigned key_rank, const entry& e) const {
    if (!depth) {
      const size_t i = lower(n, e.key), size = n ? n->size : 0;
      return leaf(size + 1, [&](size_t j) { return j == i ? e : entries(n)[j - (j > i)]; });
    }
    const size_t i = branch(n, e.key), size = n ? n->size : 0;
    if (depth == key_rank) {
      auto [left, right] = split_insert(n ? children(n)[i] : nullptr, depth - 1, e);
      return internal(depth, size + 1,
        [&](size_t j) { return j == i ? e.key : keys(n)[j - (j > i)]; },
        [&](size_t j) { return j == i ? left.p : j == i + 1 ? right.p : children(n)[j - (j > i + 1)]; });
    }
    auto child = insert(n ? children(n)[i] : nullptr, depth - 1, key_rank, e);
    return with_child(n, depth, i, child.p);
  }
  owned_node join(node* left, node* right, unsigned depth) const {
    if (!left) return hold(right);
    if (!right) return hold(left);
    const size_t a = left->size, b = right->size;
    if (!depth) return leaf(a + b, [&](size_t j) { return j < a ? entries(left)[j] : entries(right)[j - a]; });
    auto middle = join(children(left)[a], children(right)[0], depth - 1);
    return internal(depth, a + b,
      [&](size_t j) { return j < a ? keys(left)[j] : keys(right)[j - a]; },
      [&](size_t j) { return j < a ? children(left)[j] : j == a ? middle.p : children(right)[j - a]; });
  }
  owned_node replace_path(node* n, const entry& e) const {
    if (!n->rank) {
      const auto i = lower(n, e.key);
      return leaf(n->size, [&](size_t j) { return j == i ? e : entries(n)[j]; });
    }
    const auto i = branch(n, e.key);
    auto child = replace_path(children(n)[i], e);
    return with_child(n, n->rank, i, child.p);
  }
  // Called only for a key known present. Removing its separator joins neighbors.
  owned_node remove_path(node* n, uint64_t k) const {
    if (!n->rank) {
      const auto i = lower(n, k);
      return leaf(n->size - 1, [&](size_t j) { return entries(n)[j + (j >= i)]; });
    }
    const auto i = branch(n, k);
    auto child = remove_path(children(n)[i], k);
    if (i && keys(n)[i - 1] == k) {
      auto merged = join(children(n)[i - 1], child.p, n->rank - 1);
      return internal(n->rank, n->size - 1,
        [&](size_t j) { return keys(n)[j + (j >= i - 1)]; },
        [&](size_t j) { return j == i - 1 ? merged.p : children(n)[j + (j >= i)]; });
    }
    return with_child(n, n->rank, i, child.p);
  }
  summary prefix(uint64_t k) const {
    summary out;
    const node* n = root_;
    while (n && n->rank) {
      const auto end = branch(n, k);
      for (size_t i = 0; i < end; ++i) { out.count += count(children(n)[i]); out.hash ^= hash(children(n)[i]); }
      n = children(n)[end];
    }
    if (n) {
      const auto end = lower(n, k);
      out.count += end;
      for (size_t i = 0; i < end; ++i) out.hash ^= entries(n)[i].hash;
    }
    return out;
  }
  template<class F> static void collect(const node* n, range r, size_t& remaining, F& f) {
    if (!n || !remaining) return;
    if (!n->rank) {
      for (size_t i = lower(n, r.low); i < n->size && remaining; ++i) {
        if (r.high && entries(n)[i].key >= *r.high) break;
        f(entries(n)[i]); --remaining;
      }
    } else {
      for (size_t i = branch(n, r.low); i <= n->size && remaining; ++i) {
        if (i && r.high && keys(n)[i - 1] >= *r.high) break;
        collect(children(n)[i], r, remaining, f);
      }
    }
  }
  basic_persistent_wide_tree result(owned_node next) const {
    basic_persistent_wide_tree out(*this);
    release(out.storage_.get(), out.root_);
    out.root_ = next.take();
    return out;
  }
  summary inspect(const node* n, unsigned depth, std::optional<uint64_t> low,
                  std::optional<uint64_t> high) const {
    if (!n) return {};
    auto require = [](bool ok) { if (!ok) throw std::logic_error("persistent wide tree invariant"); };
    require(n->rank == depth && n->references.load(std::memory_order_relaxed) > 0 && n->count > 0);
    summary out;
    if (!depth) {
      require(n->size > 0);
      for (size_t i = 0; i < n->size; ++i) {
        const auto k = entries(n)[i].key;
        require((!low || k >= *low) && (!high || k < *high));
        require(!i || entries(n)[i - 1].key < k);
        ++out.count; out.hash ^= entries(n)[i].hash;
      }
    } else {
      for (size_t i = 0; i < n->size; ++i) {
        const auto k = keys(n)[i];
        require(rank(k) == depth && (!low || k >= *low) && (!high || k < *high));
        require(!i || keys(n)[i - 1] < k);
        auto* first = children(n)[i + 1];
        while (first && first->rank) first = children(first)[0];
        require(first && entries(first)[0].key == k);
      }
      for (size_t i = 0; i <= n->size; ++i) {
        const auto c = inspect(children(n)[i], depth - 1,
          i ? std::optional<uint64_t>(keys(n)[i - 1]) : low,
          i < n->size ? std::optional<uint64_t>(keys(n)[i]) : high);
        out.count += c.count; out.hash ^= c.hash;
      }
    }
    require(out.count == n->count && out.hash == n->hash);
    return out;
  }
  static uint64_t shape(const node* n) {
    if (!n) return 0xCAFE;
    uint64_t h = n->size + uint64_t(n->rank) * 131;
    if (!n->rank) for (size_t i = 0; i < n->size; ++i) h = std::rotl(h, 7) ^ entries(n)[i].key;
    else for (size_t i = 0; i <= n->size; ++i) {
      h = std::rotl(h, 7) ^ shape(children(n)[i]);
      if (i < n->size) h = std::rotl(h, 7) ^ keys(n)[i];
    }
    return h;
  }
 public:
  explicit basic_persistent_wide_tree(Allocator allocator = {})
      : storage_(std::make_shared<storage>(std::move(allocator))) {}
  basic_persistent_wide_tree(const basic_persistent_wide_tree& other) noexcept
      : storage_(other.storage_), root_(other.root_) { retain(root_); }
  basic_persistent_wide_tree(basic_persistent_wide_tree&& other) noexcept
      : storage_(other.storage_), root_(std::exchange(other.root_, nullptr)) {}
  basic_persistent_wide_tree& operator=(const basic_persistent_wide_tree& other) noexcept {
    if (this != &other) { basic_persistent_wide_tree copy(other); swap(copy); }
    return *this;
  }
  basic_persistent_wide_tree& operator=(basic_persistent_wide_tree&& other) noexcept {
    if (this != &other) { basic_persistent_wide_tree moved(std::move(other)); swap(moved); }
    return *this;
  }
  ~basic_persistent_wide_tree() { release(storage_.get(), root_); }
  void swap(basic_persistent_wide_tree& other) noexcept { storage_.swap(other.storage_); std::swap(root_, other.root_); }
  uint64_t size() const noexcept { return count(root_); }
  bool empty() const noexcept { return !root_; }
  uint64_t fingerprint() const noexcept { return hash(root_); }
  bool shares_root_with(const basic_persistent_wide_tree& other) const noexcept { return root_ == other.root_; }
  const entry* find(uint64_t k) const {
    const node* n = root_;
    while (n && n->rank) n = children(n)[branch(n, k)];
    if (!n) return {};
    const auto i = lower(n, k);
    return i < n->size && entries(n)[i].key == k ? entries(n)+i : nullptr;
  }
  std::optional<entry> get(uint64_t k) const {
    if(auto e=find(k)) return *e;
    return {};
  }
  [[nodiscard]] basic_persistent_wide_tree set(uint64_t k, const Value& value, uint64_t fingerprint) const {
    entry e{k, value, fingerprint};
    if (auto old = get(k)) {
      if (*old == e) return *this;
      return result(replace_path(root_, e));
    }
    auto lifted = hold(root_);
    const auto kr = rank(k);
    unsigned depth = root_ ? root_->rank : kr;
    while (depth < kr) {
      auto parent = internal(++depth, 0, [](size_t) { return uint64_t(0); }, [&](size_t) { return lifted.p; });
      lifted = std::move(parent);
    }
    return result(insert(lifted.p, depth, kr, e));
  }
  [[nodiscard]] basic_persistent_wide_tree remove(uint64_t k) const {
    if (!get(k)) return *this;
    auto next = remove_path(root_, k);
    while (next.p && next.p->rank && !next.p->size) next = hold(children(next.p)[0]);
    return result(std::move(next));
  }
  summary summarize(range r) const {
    if (r.empty()) return {};
    const auto lo = prefix(r.low), hi = r.high ? prefix(*r.high) : summary{size(), fingerprint()};
    return {hi.count - lo.count, hi.hash ^ lo.hash};
  }
  std::optional<entry> select(uint64_t index) const {
    if (index >= size()) return {};
    const node* n = root_;
    while (n->rank) {
      size_t i = 0;
      while (index >= count(children(n)[i])) { index -= count(children(n)[i]); ++i; }
      n = children(n)[i];
    }
    return entries(n)[index];
  }
  // Callback references are borrowed for this call only; returned entries from
  // get/select/list own their Value copies and can outlive every map value.
  template<class F> void for_each(range r, size_t limit, F&& f) const {
    if (!r.empty()) collect(root_, r, limit, f);
  }
  std::vector<entry> list(range r = {}, size_t limit = SIZE_MAX) const {
    std::vector<entry> out;
    out.reserve(std::min<uint64_t>(limit, summarize(r).count));
    for_each(r, limit, [&](const entry& e) { out.push_back(e); });
    return out;
  }
  std::vector<uint64_t> cuts(range r, size_t fanout) const {
    std::vector<uint64_t> out;
    const auto n = summarize(r).count;
    if (n < 2 || fanout < 2) return out;
    fanout = std::min<uint64_t>(fanout, n);
    const auto start = prefix(r.low).count;
    for (size_t i = 1; i < fanout; ++i)
      out.push_back(select(start + (n / fanout) * i + std::min<uint64_t>(i, n % fanout))->key);
    return out;
  }
  void validate() const {
    if (root_ && root_->rank && !root_->size) throw std::logic_error("uncollapsed root");
    (void)inspect(root_, root_ ? root_->rank : 0, {}, {});
  }
  uint64_t shape_digest() const { return shape(root_); }
};
using persistent_wide_tree = basic_persistent_wide_tree<>;
} // namespace datafriend
