// Mutable adaptation of ClojureDart's HashRankedWideTreapMap algorithm.
// Original copyright (c) Baptiste Dupuch & Christophe Grand. All rights reserved.
// Eclipse Public License 1.0; see ../../licenses/epl-v10.html and ../../NOTICE.md.
// This notice must not be removed. C++ adaptation and augmentation: DrawDo.
#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <vector>
#include "tree_allocator.h"

namespace datafriend {

struct record {
  uint64_t key, value, hash;
  bool operator==(const record&) const = default;
};

// Half-open range; null high means infinity, so UINT64_MAX remains a valid key.
struct key_range {
  uint64_t low = 0;
  std::optional<uint64_t> high;
  bool contains(uint64_t k) const { return k >= low && (!high || k < *high); }
  bool empty() const { return high && low >= *high; }
};

struct range_summary { uint64_t count = 0, hash = 0; };

// Single-owner mutable prototype. Not a persistent map and not thread safe.
// Mutations preserve logical contents on allocation failure (std::bad_alloc).
// Internal arrays: child, separator, child, ...; leaf arrays: key, value, hash, ...
// Rank is implicit in depth. Only the tree's top rank is stored.
template<class Allocator = malloc_nodes, bool TrackAllocations = true> class basic_wide_tree {
  struct node;
  union slot { uint64_t word; node* child; };
  struct node {
    uint64_t count, hash;
    uint32_t size, capacity; // slots used/allocated, excluding header
    slot* data() { return reinterpret_cast<slot*>(this + 1); }
    const slot* data() const { return reinterpret_cast<const slot*>(this + 1); }
  };
  static_assert(sizeof(slot) == 8 && sizeof(node) == 24);

 public:
  // Non-owning, relocatable state for explicit-lifetime namespace tables. The
  // owner must destroy it exactly once; take_state transfers ownership out.
  struct state { node* root=nullptr; unsigned rank=0; };
  using key_hash_fn = uint64_t (*)(uint64_t);
  struct allocation_stats {
    size_t live_bytes = 0, peak_bytes = 0, live_nodes = 0;
    size_t allocations = 0, copied_bytes = 0;
  };

  explicit basic_wide_tree(key_hash_fn key_hash, Allocator allocator = {})
      : key_hash_(key_hash), allocator_(std::move(allocator)) {
    if (!key_hash) throw std::invalid_argument("key hash required");
  }
  ~basic_wide_tree() { destroy(root_, top_); }
  basic_wide_tree(const basic_wide_tree&) = delete;
  basic_wide_tree& operator=(const basic_wide_tree&) = delete;

  uint64_t size() const { return count(root_); }
  uint64_t fingerprint() const { return hash(root_); }
  const allocation_stats& allocations() const { return stats_; }
  const Allocator& allocator() const { return allocator_; }
  void adopt_state(state s) { assert(!root_); root_=s.root; top_=s.rank; }
  state take_state() { state s{root_,top_}; root_=nullptr; top_=0; return s; }

  std::optional<record> get(uint64_t key) const {
    const node* n = root_;
    for (unsigned rank = top_; n && rank; --rank)
      n = n->data()[2 * branch(n, key)].child;
    if (!n) return {};
    const size_t i = leaf_lower(n, key);
    if (i == n->size || n->data()[i].word != key) return {};
    return entry(n, i);
  }

  // Returns true for insertion, false for replacement. Existing keys never move.
  bool set(record r) {
    if (replace(root_, top_, r)) return false;
    const unsigned rank = key_rank(r.key);
    const unsigned old_top=top_;
    try {
      while (top_ < rank) {
        node* parent = make(1);
        parent->data()[0].child = root_;
        refresh(parent, ++top_);
        root_ = parent;
      }
      insert(root_, top_, rank, r);
    } catch(const std::bad_alloc&) {
      while(top_>old_top) {
        node* child=root_->data()[0].child;
        release(root_); root_=child; --top_;
      }
      throw;
    }
    return true;
  }

  bool replace_existing(record r) { return replace(root_,top_,r); }

  bool erase(uint64_t key) {
    if (!remove(root_, top_, key)) return false;
    while (root_ && top_ && root_->size == 1) {
      node* child = root_->data()[0].child;
      release(root_);
      root_ = child;
      --top_;
    }
    if (!root_) top_ = 0;
    return true;
  }

  range_summary summarize(key_range r) const {
    if (r.empty()) return {};
    const auto lo = prefix(r.low);
    const auto hi = r.high ? prefix(*r.high) : range_summary{size(), fingerprint()};
    return {hi.count - lo.count, hi.hash ^ lo.hash};
  }

  std::optional<record> select(uint64_t index) const {
    if (index >= size()) return {};
    const node* n = root_;
    for (unsigned rank = top_; rank; --rank) {
      size_t i = 0;
      while (index >= count(n->data()[i].child)) {
        index -= count(n->data()[i].child);
        i += 2;
        assert(i < n->size);
      }
      n = n->data()[i].child;
    }
    return entry(n, size_t(index) * 3);
  }

  // Bounded, ordered enumeration. No pointers into mutable nodes escape.
  std::vector<record> list(key_range r, size_t limit) const {
    std::vector<record> out;
    out.reserve(std::min<uint64_t>(limit, size()));
    if (!r.empty() && limit) collect(root_, top_, r, limit, out);
    return out;
  }

  // Allocation-free ordered traversal. The callback must not mutate this tree.
  template<class F> void for_each(F&& f) const { visit(root_,top_,f); }

  // Equal-cardinality splits using cached counts; includes empty outer gaps.
  std::vector<uint64_t> cuts(key_range r, size_t fanout) const {
    std::vector<uint64_t> out;
    const auto n = summarize(r).count;
    if (n < 2 || fanout < 2) return out;
    fanout = std::min<uint64_t>(fanout, n);
    const auto start = prefix(r.low).count;
    for (size_t i = 1; i < fanout; ++i) {
      const auto offset = (n / fanout) * i + std::min<uint64_t>(i, n % fanout);
      out.push_back(select(start + offset)->key);
    }
    return out;
  }

  // Expensive independent invariant walk for tests, never used in timings.
  void validate() const { (void)inspect(root_, top_, {}, {}); }
  uint64_t shape_digest() const { return shape(root_, top_); }

 private:
  node* root_ = nullptr;
  unsigned top_ = 0;
  key_hash_fn key_hash_;
  allocation_stats stats_;
  [[no_unique_address]] Allocator allocator_;

  static uint64_t count(const node* n) { return n ? n->count : 0; }
  static uint64_t hash(const node* n) { return n ? n->hash : 0; }
  unsigned key_rank(uint64_t k) const {
    return std::countr_zero(uint32_t(key_hash_(k))) / 4;
  }
  static record entry(const node* n, size_t i) {
    return {n->data()[i].word, n->data()[i+1].word, n->data()[i+2].word};
  }
  template<class F> static void visit(const node* n,unsigned rank,F& f) {
    if(!n) return;
    if(rank) {
      for(size_t i=0;i<n->size;i+=2) visit(n->data()[i].child,rank-1,f);
    } else {
      for(size_t i=0;i<n->size;i+=3) f(entry(n,i));
    }
  }
  static size_t bytes(const node* n) { return sizeof(node) + n->capacity * sizeof(slot); }
  node* make(size_t size) {
    // 25% size classes, with a minimum of four slots. Space is reused via malloc.
    size_t capacity = 4;
    while (capacity < size) capacity += std::max<size_t>(1, capacity / 4);
    if (capacity > UINT32_MAX || capacity > (SIZE_MAX - sizeof(node)) / sizeof(slot))
      throw std::bad_alloc();
    const size_t allocation = sizeof(node) + capacity * sizeof(slot);
    void* memory = allocator_.allocate(allocation);
    if (!memory) throw std::bad_alloc();
    node* n = new (memory) node{0, 0, uint32_t(size), uint32_t(capacity)};
    if constexpr(TrackAllocations) {
      stats_.live_bytes += allocation;
      stats_.peak_bytes = std::max(stats_.peak_bytes, stats_.live_bytes);
      ++stats_.live_nodes; ++stats_.allocations;
    }
    return n;
  }
  void release(node* n) {
    if (!n) return;
    const auto allocation = bytes(n);
    if constexpr(TrackAllocations) { stats_.live_bytes -= allocation; --stats_.live_nodes; }
    n->~node();
    allocator_.deallocate(n, allocation);
  }
  void reserve(node*& n, size_t size) {
    if (n && n->capacity >= size) return;
    node* next = make(size);
    next->size = n ? n->size : 0;
    if (n) {
      next->count = n->count; next->hash = n->hash;
      std::memcpy(next->data(), n->data(), n->size * sizeof(slot));
      if constexpr(TrackAllocations) stats_.copied_bytes += n->size * sizeof(slot);
      release(n);
    }
    n = next;
  }
  void trim(node*& n) noexcept {
    if (!n) return;
    if (n->count == 0) { destroy(n, 0); n = nullptr; return; }
    // Hysteresis avoids retaining huge arrays after deletion without resizing every edit.
    if (n->size >= n->capacity / 2 || n->capacity <= 8) return;
    node* next;
    try { next=make(n->size); } catch(const std::bad_alloc&) { return; }
    next->count = n->count; next->hash = n->hash;
    std::memcpy(next->data(), n->data(), n->size * sizeof(slot));
    if constexpr(TrackAllocations) stats_.copied_bytes += n->size * sizeof(slot);
    release(n); n = next;
  }
  void destroy(node* n, unsigned rank) {
    if (!n) return;
    if (rank) for (size_t i = 0; i < n->size; i += 2) destroy(n->data()[i].child, rank-1);
    release(n);
  }
  static size_t leaf_lower(const node* n, uint64_t k) {
    size_t lo = 0, hi = n->size / 3;
    while (lo < hi) {
      const auto mid = lo + (hi-lo)/2;
      if (n->data()[3*mid].word < k) lo = mid+1; else hi = mid;
    }
    return 3*lo;
  }
  static size_t branch(const node* n, uint64_t k) {
    size_t lo = 0, hi = n->size / 2;
    while (lo < hi) {
      const auto mid = lo + (hi-lo)/2;
      if (n->data()[2*mid+1].word <= k) lo = mid+1; else hi = mid;
    }
    return lo;
  }
  static void refresh(node* n, unsigned rank) {
    n->count = n->hash = 0;
    if (rank) {
      for (size_t i = 0; i < n->size; i += 2) {
        n->count += count(n->data()[i].child);
        n->hash ^= hash(n->data()[i].child);
      }
    } else {
      n->count = n->size / 3;
      for (size_t i = 2; i < n->size; i += 3) n->hash ^= n->data()[i].word;
    }
  }
  static bool replace(node* n, unsigned rank, record r) {
    if (!n) return false;
    if (!rank) {
      const auto i = leaf_lower(n, r.key);
      if (i == n->size || n->data()[i].word != r.key) return false;
      n->hash ^= n->data()[i+2].word ^ r.hash;
      n->data()[i+1].word = r.value; n->data()[i+2].word = r.hash;
      return true;
    }
    node* child = n->data()[2*branch(n, r.key)].child;
    const auto before = hash(child);
    if (!replace(child, rank-1, r)) return false;
    n->hash ^= before ^ hash(child);
    return true;
  }
  void leaf_insert(node*& n, record r, size_t i) {
    const size_t old = n ? n->size : 0;
    reserve(n, old+3);
    std::memmove(n->data()+i+3, n->data()+i, (old-i)*sizeof(slot));
    if constexpr(TrackAllocations) stats_.copied_bytes += (old-i)*sizeof(slot);
    n->data()[i].word = r.key; n->data()[i+1].word = r.value;
    n->data()[i+2].word = r.hash;
    n->size += 3; ++n->count; n->hash ^= r.hash;
  }
  // Split and prepend the new record together: allocate on descent, then
  // mutate on unwind. A failure never happens after records have moved.
  std::pair<node*, node*> split_insert(node* old, unsigned rank, record r) {
    if (!rank) {
      const auto i=old ? leaf_lower(old,r.key) : 0;
      if(!i) { leaf_insert(old,r,0); return {nullptr,old}; }
      node* right=make(old->size-i+3);
      right->data()[0].word=r.key; right->data()[1].word=r.value;
      right->data()[2].word=r.hash;
      std::memcpy(right->data()+3,old->data()+i,(old->size-i)*sizeof(slot));
      if constexpr(TrackAllocations) stats_.copied_bytes += (old->size-i)*sizeof(slot);
      old->size=uint32_t(i); refresh(old,0); refresh(right,0); trim(old);
      return {old,right};
    }
    const auto i=old ? 2*branch(old,r.key) : 0;
    node* right=make(old ? old->size-i : 1);
    std::pair<node*,node*> children;
    try { children=split_insert(old ? old->data()[i].child : nullptr,rank-1,r); }
    catch(const std::bad_alloc&) { release(right); throw; }
    if(old) {
      std::memcpy(right->data(),old->data()+i,right->size*sizeof(slot));
      if constexpr(TrackAllocations) stats_.copied_bytes += right->size*sizeof(slot);
      old->data()[i].child=children.first; old->size=uint32_t(i+1);
      refresh(old,rank);
      if(!old->count) { destroy(old,rank); old=nullptr; } else trim(old);
    }
    right->data()[0].child=children.second; refresh(right,rank);
    return {old,right};
  }
  void insert(node*& n, unsigned rank, unsigned kr, record r) {
    if (!rank) { leaf_insert(n,r,n ? leaf_lower(n,r.key) : 0); return; }
    const bool created=!n;
    if(created) { n=make(1); n->data()[0].child=nullptr; }
    try {
      const size_t i=2*branch(n,r.key);
      if(rank==kr) {
        const size_t old=n->size;
        reserve(n,old+2); // complete the parent allocation before changing children
        auto [left,right]=split_insert(n->data()[i].child,rank-1,r);
        std::memmove(n->data()+i+3,n->data()+i+1,(old-i-1)*sizeof(slot));
        if constexpr(TrackAllocations) stats_.copied_bytes += (old-i-1)*sizeof(slot);
        n->data()[i].child=left; n->data()[i+1].word=r.key;
        n->data()[i+2].child=right; n->size+=2;
      } else insert(n->data()[i].child,rank-1,kr,r);
    } catch(const std::bad_alloc&) {
      if(created) { release(n); n=nullptr; }
      throw;
    }
    ++n->count; n->hash^=r.hash;
  }
  // Removing a separator joins its neighbors. Reserve the joining boundary
  // before removing the record. Successful reserves can change capacity on
  // failure, but preserve contents, ordering, counts and fingerprints.
  void prepare_join(node*& left,const node* right,unsigned rank) {
    if(!left || !right || right->count==1) return;
    if(!rank) reserve(left,size_t(left->size)+right->size-3);
    else {
      reserve(left,size_t(left->size)+right->size-1);
      prepare_join(left->data()[left->size-1].child,right->data()[0].child,rank-1);
    }
  }
  node* join(node* left, node* right, unsigned rank) {
    if (!left) return right;
    if (!right) return left;
    const size_t old = left->size;
    if (!rank) {
      assert(left->capacity>=old+right->size); // prepared before removing the key
      reserve(left, old+right->size);
      std::memcpy(left->data()+old, right->data(), right->size*sizeof(slot));
      if constexpr(TrackAllocations) stats_.copied_bytes += right->size*sizeof(slot);
      left->size += right->size;
    } else {
      node* middle = join(left->data()[old-1].child, right->data()[0].child, rank-1);
      assert(left->capacity>=old+right->size-1);
      reserve(left, old+right->size-1);
      left->data()[old-1].child = middle;
      std::memcpy(left->data()+old, right->data()+1, (right->size-1)*sizeof(slot));
      if constexpr(TrackAllocations) stats_.copied_bytes += (right->size-1)*sizeof(slot);
      left->size += right->size-1;
    }
    release(right); refresh(left, rank);
    return left;
  }
  bool remove(node*& n, unsigned rank, uint64_t k) {
    if (!n) return false;
    if (!rank) {
      const auto i = leaf_lower(n, k);
      if (i == n->size || n->data()[i].word != k) return false;
      n->hash ^= n->data()[i+2].word; --n->count;
      std::memmove(n->data()+i, n->data()+i+3, (n->size-i-3)*sizeof(slot));
      if constexpr(TrackAllocations) stats_.copied_bytes += (n->size-i-3)*sizeof(slot);
      n->size -= 3; trim(n); return true;
    }
    const auto i = 2*branch(n, k);
    if(i && n->data()[i-1].word==k)
      prepare_join(n->data()[i-2].child,n->data()[i].child,rank-1);
    if (!remove(n->data()[i].child, rank-1, k)) return false;
    if (i && n->data()[i-1].word == k) {
      n->data()[i-2].child = join(n->data()[i-2].child, n->data()[i].child, rank-1);
      std::memmove(n->data()+i-1, n->data()+i+1, (n->size-i-1)*sizeof(slot));
      if constexpr(TrackAllocations) stats_.copied_bytes += (n->size-i-1)*sizeof(slot);
      n->size -= 2;
    }
    refresh(n, rank);
    if (!n->count) { destroy(n, rank); n = nullptr; } else trim(n);
    return true;
  }
  range_summary prefix(uint64_t key) const {
    range_summary out;
    const node* n = root_;
    for (unsigned rank = top_; n && rank; --rank) {
      const auto end = 2*branch(n, key);
      for (size_t i = 0; i < end; i += 2) {
        out.count += count(n->data()[i].child); out.hash ^= hash(n->data()[i].child);
      }
      n = n->data()[end].child;
    }
    if (n) {
      const auto end = leaf_lower(n, key);
      out.count += end/3;
      for (size_t i = 2; i < end; i += 3) out.hash ^= n->data()[i].word;
    }
    return out;
  }
  static void collect(const node* n, unsigned rank, key_range r, size_t limit,
                      std::vector<record>& out) {
    if (!n) return;
    if (!rank) {
      for (size_t i = leaf_lower(n, r.low); i < n->size && out.size() < limit; i += 3) {
        const auto e = entry(n, i);
        if (r.high && e.key >= *r.high) break;
        out.push_back(e);
      }
      return;
    }
    for (size_t i = 2*branch(n, r.low); i < n->size && out.size() < limit; i += 2) {
      if (i && r.high && n->data()[i-1].word >= *r.high) break;
      collect(n->data()[i].child, rank-1, r, limit, out);
    }
  }
  range_summary inspect(const node* n, unsigned rank, std::optional<uint64_t> low,
                        std::optional<uint64_t> high) const {
    if (!n) return {};
    auto require = [](bool ok) { if (!ok) throw std::logic_error("wide tree invariant"); };
    require(n->size <= n->capacity && n->size != 0);
    range_summary out;
    if (!rank) {
      require(n->size % 3 == 0);
      for (size_t i = 0; i < n->size; i += 3) {
        const auto e = entry(n, i);
        require((!low || e.key >= *low) && (!high || e.key < *high));
        require(i == 0 || n->data()[i-3].word < e.key);
        ++out.count; out.hash ^= e.hash;
      }
    } else {
      require(n->size % 2 == 1);
      for (size_t i = 1; i < n->size; i += 2) {
        const auto k = n->data()[i].word;
        require(key_rank(k) == rank && (!low || k >= *low) && (!high || k < *high));
        require(i == 1 || n->data()[i-2].word < k);
        const node* first = n->data()[i+1].child;
        for (unsigned d = rank-1; first && d; --d) first = first->data()[0].child;
        require(first && first->data()[0].word == k);
      }
      for (size_t i = 0; i < n->size; i += 2) {
        const auto c = inspect(n->data()[i].child, rank-1,
          i ? std::optional<uint64_t>(n->data()[i-1].word) : low,
          i+1 < n->size ? std::optional<uint64_t>(n->data()[i+1].word) : high);
        out.count += c.count; out.hash ^= c.hash;
      }
    }
    require(out.count == n->count && out.hash == n->hash);
    return out;
  }
  static uint64_t shape(const node* n, unsigned rank) {
    if (!n) return 0xCAFE;
    uint64_t h = n->size + uint64_t(rank)*131;
    for (size_t i = 0; i < n->size; i += rank ? 1 : 3) {
      const auto x = rank && i%2 == 0 ? shape(n->data()[i].child, rank-1) : n->data()[i].word;
      h = std::rotl(h, 7) ^ x;
    }
    return h;
  }
};
using wide_tree = basic_wide_tree<>;
using slab_wide_tree = basic_wide_tree<slab_nodes>;
} // namespace datafriend
