#pragma once
#include "support.h"
#include <unordered_map>
#include <memory>
#include <type_traits>
#include "negentropy/storage/BTreeMem.h"
#include "btree_xor_core.h"

namespace experiment {
inline uint64_t read64(const uint8_t* p) {
  uint64_t v = 0; for (unsigned i = 0; i < 8; ++i) v = (v << 8) | p[i]; return v;
}
struct original_btree_types {
  using Core = negentropy::storage::btree::BTreeCore;
  using Node = negentropy::storage::btree::Node;
  using NodePtr = negentropy::storage::btree::NodePtr;
  using Item = negentropy::Item;
  using Bound = negentropy::Bound;
  using Accumulator = negentropy::Accumulator;
  static constexpr bool native_xor = false;
  static Item encode(record r) {
    uint8_t id[32]{};
    put64(id,r.key); put64(id+8,r.value); put64(id+16,r.hash);
    return Item(r.key, {reinterpret_cast<const char*>(id),32});
  }
  static record decode(const Item& i) {
    return {i.timestamp, read64(i.id+8), read64(i.id+16)};
  }
  static uint64_t digest(const negentropy::Fingerprint& f) { return read64(f.buf); }
};
struct xor_btree_types {
  using Core = negentropy_xor::storage::btree::BTreeCore;
  using Node = negentropy_xor::storage::btree::Node;
  using NodePtr = negentropy_xor::storage::btree::NodePtr;
  using Item = negentropy_xor::Item;
  using Bound = negentropy_xor::Bound;
  using Accumulator = negentropy_xor::Accumulator;
  static constexpr bool native_xor = true;
  static Item encode(record r) { Item i; i.timestamp=r.key; i.value=r.value; i.hash=r.hash; return i; }
  static record decode(const Item& i) { return {i.timestamp,i.value,i.hash}; }
  static uint64_t digest(const negentropy_xor::Fingerprint& f) { return f.hash; }
};

// Same node-ID/unordered_map storage operations as upstream BTreeMem. Sharing
// this pool among namespace roots matches LittleBear's shared binary arena.
template<class T> struct btree_pool {
  std::unordered_map<uint64_t, typename T::Node> nodes;
  uint64_t next = 1;
  size_t bytes_lower_bound() const {
    return nodes.size()*sizeof(typename decltype(nodes)::value_type) + nodes.bucket_count()*sizeof(void*);
  }
};

template<class T> class btree_index final : public T::Core {
 public:
  using Core = typename T::Core;
  using Node = typename T::Node;
  using NodePtr = typename T::NodePtr;
  using pool_type = btree_pool<T>;
  static constexpr bool native_xor = T::native_xor;
 private:
  std::unique_ptr<pool_type> owner_;
  pool_type* pool_;
  uint64_t root_ = 0;
 public:
  btree_index() : owner_(std::make_unique<pool_type>()), pool_(owner_.get()) {}
  explicit btree_index(pool_type& pool) : pool_(&pool) {}
  ~btree_index() { if (!owner_) clear_nodes(root_); }
  btree_index(const btree_index&) = delete;
  const NodePtr getNodeRead(uint64_t id) override {
    const auto it = pool_->nodes.find(id);
    return it == pool_->nodes.end() ? NodePtr{nullptr,0} : NodePtr{&it->second,id};
  }
  NodePtr getNodeWrite(uint64_t id) override { return getNodeRead(id); }
  NodePtr makeNode() override {
    const auto id = pool_->next++;
    pool_->nodes.try_emplace(id); return getNodeRead(id);
  }
  void deleteNode(uint64_t id) override { pool_->nodes.erase(id); }
  uint64_t getRootNodeId() override { return root_; }
  void setRootNodeId(uint64_t id) override { root_=id; }
  using Core::size;
  using Core::fingerprint;
  uint64_t size() const { return const_cast<btree_index*>(this)->Core::size(); }

  std::optional<record> get(uint64_t k) {
    auto position = locate(k);
    if (!position.first) return {};
    return T::decode(position.first->items[position.second].item);
  }
  bool set(record r) {
    const auto old = get(r.key);
    // Preserve upstream insert/erase behavior in BOTH variants. No special
    // in-place update optimization is silently added to the adapted B-tree.
    if (old) require(Core::eraseItem(T::encode(*old)), "btree replacement erase");
    require(Core::insertItem(T::encode(r)), "btree insertion");
    return !old;
  }
  bool erase(uint64_t k) {
    auto e = get(k); return e && Core::eraseItem(T::encode(*e));
  }
  uint64_t fingerprint() { return T::digest(Core::fingerprint(0,Core::size())); }
  uint64_t range_hash(key_range r) {
    if (r.empty()) {
      typename T::Accumulator a; a.setToZero(); return T::digest(a.getFingerprint(0));
    }
    const auto [lo,hi] = offsets(r); return T::digest(Core::fingerprint(lo,hi));
  }
  std::vector<record> list(key_range r, size_t limit) {
    std::vector<record> out;
    if (r.empty() || !limit || !Core::size()) return out;
    const auto [lo,hi] = offsets(r);
    const auto end = lo+std::min<uint64_t>(hi-lo,limit);
    out.reserve(end-lo);
    Core::iterate(lo,end,[&](const auto& item,size_t) { out.push_back(T::decode(item)); return true; });
    return out;
  }
  std::vector<uint64_t> cuts(key_range r, size_t fanout) {
    std::vector<uint64_t> out;
    if (r.empty()) return out;
    const auto [lo,hi]=offsets(r);
    const auto n=hi-lo; fanout=std::min<uint64_t>(n,fanout);
    for (size_t i=1;i<fanout;++i)
      out.push_back(Core::getItem(lo+(n/fanout)*i+std::min<uint64_t>(i,n%fanout)).timestamp);
    return out;
  }
  size_t reserved_bytes() const { return pool_->bytes_lower_bound(); }
  static uint64_t expected_hash(const std::vector<record>& records) {
    typename T::Accumulator a; a.setToZero();
    for (auto r: records) a.add(T::encode(r));
    return T::digest(a.getFingerprint(records.size()));
  }
  void validate() {
    std::set<uint64_t> seen;
    auto walk = [&](auto&& self,uint64_t id)->std::pair<uint64_t,typename T::Accumulator> {
      typename T::Accumulator a; a.setToZero(); uint64_t count=0;
      require(seen.insert(id).second,"btree duplicate node");
      auto ptr=getNodeRead(id); require(ptr.exists(),"btree missing node");
      auto& n=ptr.get(); require(n.numItems>0 && n.numItems<=80,"btree node width");
      for(size_t i=0;i<n.numItems;++i) {
        require(i==0 || n.items[i-1].item<n.items[i].item,"btree order");
        if(n.items[i].nodeId) {
          const auto& child=getNodeRead(n.items[i].nodeId).get();
          require(T::decode(child.items[0].item)==T::decode(n.items[i].item),"btree separator");
          auto [c,h]=self(self,n.items[i].nodeId); count+=c; a.add(h);
        } else { ++count; a.add(n.items[i].item); }
      }
      require(count==n.accumCount && T::digest(a.getFingerprint(count))==T::digest(n.accum.getFingerprint(count)),"btree aggregate");
      return {count,a};
    };
    if(root_) (void)walk(walk,root_);
  }
 private:
  std::pair<Node*,size_t> locate(uint64_t k) {
    if(!root_) return {nullptr,0};
    bool found=false;
    const auto path=Core::searchItem(root_,typename T::Bound(k).item,found);
    auto* node=path.back().nodePtr.p; auto i=path.back().index;
    if(node->items[i].item.timestamp<k) ++i;
    if(i==node->numItems) { node=getNodeRead(node->nextSibling).p; i=0; }
    if(!node || node->items[i].item.timestamp!=k) return {nullptr,0};
    return {node,i};
  }
  std::pair<size_t,size_t> offsets(key_range r) {
    const auto size=Core::size();
    const auto lo=Core::findLowerBound(0,size,typename T::Bound(r.low));
    const auto hi=r.high ? Core::findLowerBound(0,size,typename T::Bound(*r.high)) : size;
    return {lo,hi};
  }
  void clear_nodes(uint64_t id) {
    if(!id) return;
    auto& n=pool_->nodes.at(id);
    for(size_t i=0;i<n.numItems;++i) if(n.items[i].nodeId) clear_nodes(n.items[i].nodeId);
    pool_->nodes.erase(id);
  }
};
using btree_original = btree_index<original_btree_types>;
using btree_xor = btree_index<xor_btree_types>;
} // namespace experiment
