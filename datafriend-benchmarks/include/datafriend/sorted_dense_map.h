#pragma once
#include <ankerl/unordered_dense.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace datafriend {
// Experimental single-owner map. Reads by key include pending edits; ordered
// traversal is valid after publish(). The owning transaction handles allocation
// failure/recovery, as with the engine's other mutable indexes.
template<class Value> class sorted_dense_map {
  using map_type=ankerl::unordered_dense::map<uint64_t,Value>;
  map_type facts_;
  ankerl::unordered_dense::map<uint64_t,std::optional<Value>> pending_;
 public:
  struct maintenance { uint64_t tail_batches=0, merges=0; };
 private:
  maintenance stats_;
  void clear_pending() {
    // clear() zeros the entire retained hash index. After a large initial
    // construction that would make even a one-key tail edit O(view size).
    // Clear densely filled batches in bulk; erase sparse batches from the end.
    if(pending_.size()<pending_.bucket_count()/8) {
      while(!pending_.empty()) pending_.erase(pending_.values().back().first);
    } else pending_.clear();
  }
 public:
  const maintenance& stats() const { return stats_; }
  const auto& values() const { assert(pending_.empty()); return facts_.values(); }
  const Value* get(uint64_t key) const {
    if(!pending_.empty()) {
      auto p=pending_.find(key);
      if(p!=pending_.end()) return p->second?&*p->second:nullptr;
    }
    auto p=facts_.find(key); return p==facts_.end()?nullptr:&p->second;
  }
  void set(uint64_t key,Value value) {
    if(!pending_.empty()) {
      auto p=pending_.find(key);
      if(p!=pending_.end()) {p->second=std::move(value);return;}
    }
    auto p=facts_.find(key);
    if(p!=facts_.end()) p->second=std::move(value);
    else pending_.emplace(key,std::move(value));
  }
  void erase(uint64_t key) {
    auto p=pending_.find(key);
    if(p!=pending_.end()) {
      if(facts_.contains(key)) p->second.reset();
      else pending_.erase(p); // an uncommitted insertion canceled in the batch
    } else if(facts_.contains(key)) pending_.emplace(key,std::nullopt);
  }
  void publish() {
    if(pending_.empty()) return;
    using entry=typename map_type::value_type;
    std::vector<entry> added;
    std::vector<uint64_t> removed;
    for(auto& [key,value]:pending_) {
      if(value) {
        auto old=facts_.find(key);
        if(old!=facts_.end()) old->second=std::move(*value); // erase then set
        else added.emplace_back(key,std::move(*value));
      } else removed.push_back(key);
    }
    std::sort(added.begin(),added.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::sort(removed.begin(),removed.end());
    const auto& old=facts_.values();
    const size_t surviving=old.size()-removed.size();
    bool tail=true;
    for(size_t i=0;i<removed.size();++i)
      if(old[surviving+i].first!=removed[i]) {tail=false;break;}
    if(tail&&!added.empty()&&surviving&&added.front().first<=old[surviving-1].first) tail=false;
    if(added.empty()&&removed.empty()) {clear_pending();return;}
    if(tail) {
      // Remove from the back, so unordered_dense never swaps another value
      // into the hole. Sorted additions then append in their final positions.
      for(size_t i=0;i<removed.size();++i) facts_.erase(facts_.values().back().first);
      for(auto& [key,value]:added) facts_.emplace(key,std::move(value));
      ++stats_.tail_batches;
    } else {
      auto values=std::move(facts_).extract();
      values.reserve(std::max(values.size(),surviving+added.size()));
      // Compact deletions in order, retaining the original sorted prefix.
      size_t dest=0,remove=0;
      for(size_t i=0;i<values.size();++i) {
        if(remove<removed.size()&&values[i].first==removed[remove]) {++remove;continue;}
        if(dest!=i) values[dest]=std::move(values[i]);
        ++dest;
      }
      assert(dest==surviving&&remove==removed.size());
      values.resize(surviving);
      values.resize(surviving+added.size());
      // Merge backwards into the same array. Only changed entries need an
      // auxiliary buffer; moving references does not copy boxed payloads.
      size_t a=surviving,b=added.size(),write=values.size();
      while(b) {
        --write;
        if(a&&values[a-1].first>added[b-1].first) values[write]=std::move(values[--a]);
        else values[write]=std::move(added[--b]);
      }
      facts_.replace(std::move(values)); // rebuild indices once, after the merge
      ++stats_.merges;
    }
    clear_pending();
  }
  size_t reserved_bytes() const {
    return sizeof(*this)+facts_.values().capacity()*sizeof(typename map_type::value_type)+facts_.index_bytes()
      +pending_.values().capacity()*sizeof(typename decltype(pending_)::value_type)+pending_.index_bytes();
  }
};
} // namespace datafriend
