// Container-only comparison: immutable boxes are owned by the fixture, and every
// layout stores the same borrowed pointers. No DB/query/FFI/decoding work is timed.
#include "datafriend/wide_tree.h"
#include "datafriend/sorted_dense_map.h"
#include <ankerl/unordered_dense.h>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <string>

namespace dense=ankerl::unordered_dense;
using clock_type=std::chrono::steady_clock;
struct box { uint64_t value; }; // fixed address, immutable during every measurement
using ref=const box*;
uint64_t key(uint32_t e,uint32_t a) { return uint64_t(e)<<32|a; }
uint64_t mix(uint64_t x) {
  x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; return x^(x>>31);
}
void require(bool ok,const char* msg) { if(!ok) throw std::runtime_error(msg); }

struct nested {
  dense::map<uint32_t,dense::map<uint32_t,ref>> rows;
  void set(uint64_t k,ref v) { rows[uint32_t(k>>32)][uint32_t(k)]=v; }
  void erase(uint64_t k) {
    auto row=rows.find(uint32_t(k>>32));
    if(row==rows.end()) return;
    row->second.erase(uint32_t(k)); if(row->second.empty()) rows.erase(row);
  }
  ref get(uint64_t k) const {
    auto row=rows.find(uint32_t(k>>32)); if(row==rows.end()) return nullptr;
    auto a=row->second.find(uint32_t(k)); return a==row->second.end()?nullptr:a->second;
  }
  void publish() {}
  template<class F> void each(F f) const {
    for(const auto& [e,row]:rows) for(const auto& [a,v]:row) f(key(e,a),v);
  }
  std::vector<uint32_t> ids(bool sorted=true) const {
    std::vector<uint32_t> out; out.reserve(rows.size());
    for(const auto& [e,row]:rows) out.push_back(e);
    if(sorted) std::sort(out.begin(),out.end()); return out;
  }
  uint64_t grouped_read(uint32_t attrs) const {
    (void)attrs; uint64_t sum=0;
    for(auto e:ids()) {
      sum+=e;
      for(const auto& [a,v]:rows.at(e)) sum+=v->value;
    }
    return sum;
  }
  size_t bytes() const {
    size_t n=sizeof(*this)+rows.values().capacity()*sizeof(decltype(rows)::value_type)+rows.index_bytes();
    for(const auto& [e,row]:rows) n+=row.values().capacity()*sizeof(std::remove_cvref_t<decltype(row)>::value_type)+row.index_bytes();
    return n;
  }
};

template<bool Sorted> struct flat {
  dense::map<uint64_t,ref> facts;
  bool dirty=false;
  void set(uint64_t k,ref v) { auto [it,added]=facts.try_emplace(k,v); it->second=v; dirty|=added; }
  void erase(uint64_t k) { dirty|=facts.erase(k)!=0; }
  ref get(uint64_t k) const { auto it=facts.find(k); return it==facts.end()?nullptr:it->second; }
  void publish() {
    if constexpr(Sorted) if(dirty) {
      auto values=std::move(facts).extract();
      std::sort(values.begin(),values.end(),[](auto a,auto b){return a.first<b.first;});
      facts.replace(std::move(values));
    }
    dirty=false;
  }
  template<class F> void each(F f) const { for(auto [k,v]:facts) f(k,v); }
  std::vector<uint32_t> ids(bool sorted=true) const {
    std::vector<uint32_t> out;
    if constexpr(Sorted) {
      for(auto [k,v]:facts) if(out.empty()||out.back()!=uint32_t(k>>32)) out.push_back(uint32_t(k>>32));
    } else {
      dense::set<uint32_t> seen;
      for(auto [k,v]:facts) if(seen.insert(uint32_t(k>>32)).second) out.push_back(uint32_t(k>>32));
      if(sorted) std::sort(out.begin(),out.end());
    }
    return out;
  }
  uint64_t grouped_read(uint32_t attrs) const {
    uint64_t sum=0;
    if constexpr(Sorted) {
      uint64_t previous=UINT64_MAX;
      for(auto [k,v]:facts) {
        const auto e=k>>32;
        if(e!=previous) { sum+=e; previous=e; }
        sum+=v->value;
      }
    } else {
      // The projection's attribute IDs are known. Visit entities in ascending
      // order and probe their fields; no per-entity scan over the whole map.
      for(auto e:ids()) {
        sum+=e;
        for(uint32_t a=1;a<=attrs;++a) sum+=get(key(e,a))->value;
      }
    }
    return sum;
  }
  size_t bytes() const { return sizeof(*this)+facts.values().capacity()*sizeof(typename decltype(facts)::value_type)+facts.index_bytes(); }
};

struct merged_dense {
  datafriend::sorted_dense_map<ref> facts;
  void set(uint64_t k,ref v) {facts.set(k,v);}
  void erase(uint64_t k) {facts.erase(k);}
  ref get(uint64_t k) const {auto v=facts.get(k);return v?*v:nullptr;}
  void publish() {facts.publish();}
  template<class F> void each(F f) const {for(auto [k,v]:facts.values()) f(k,v);}
  std::vector<uint32_t> ids([[maybe_unused]] bool sorted=true) const {
    std::vector<uint32_t> out;
    for(auto [k,v]:facts.values()) if(out.empty()||out.back()!=uint32_t(k>>32)) out.push_back(uint32_t(k>>32));
    return out;
  }
  uint64_t grouped_read([[maybe_unused]] uint32_t attrs) const {
    uint64_t sum=0,previous=UINT64_MAX;
    for(auto [k,v]:facts.values()) {
      auto e=k>>32;if(e!=previous){sum+=e;previous=e;}sum+=v->value;
    }
    return sum;
  }
  size_t bytes() const {return facts.reserved_bytes();}
};

struct wide {
  // The existing augmented tree, including count/hash maintenance. Zero hashes
  // are intentional: views don't need reconciliation aggregates.
  // Reuse slabs across view lifetimes, as the backend does across maps.
  static datafriend::slab_pool& pool() { static datafriend::slab_pool p; return p; }
  datafriend::basic_wide_tree<datafriend::slab_nodes,false> tree{mix,datafriend::slab_nodes(pool())};
  void set(uint64_t k,ref v) { tree.set({k,uint64_t(reinterpret_cast<uintptr_t>(v)),0}); }
  void erase(uint64_t k) { tree.erase(k); }
  ref get(uint64_t k) const { auto r=tree.get(k); return r?reinterpret_cast<ref>(uintptr_t(r->value)):nullptr; }
  void publish() {}
  template<class F> void each(F f) const { tree.for_each([&](auto r){f(r.key,reinterpret_cast<ref>(uintptr_t(r.value)));}); }
  std::vector<uint32_t> ids([[maybe_unused]] bool sorted=true) const {
    std::vector<uint32_t> out;
    each([&](auto k,ref){if(out.empty()||out.back()!=uint32_t(k>>32)) out.push_back(uint32_t(k>>32));});
    return out;
  }
  uint64_t grouped_read(uint32_t attrs) const {
    (void)attrs; uint64_t sum=0, previous=UINT64_MAX;
    each([&](auto k,ref v){auto e=k>>32; if(e!=previous){sum+=e;previous=e;} sum+=v->value;});
    return sum;
  }
  size_t bytes() const {return sizeof(*this)+sizeof(datafriend::slab_pool)+tree.allocator().pool->stats().reserved_bytes;}
};

// Keep every observed result live without putting a volatile access in a loop.
uint64_t observed=0;
template<class F> void measure(const char* name,size_t samples,size_t inner,F action) {
  std::vector<double> times; times.reserve(samples);
  for(size_t i=0;i<samples+20;++i) {
    uint64_t sum=0; const auto begin=clock_type::now();
    for(size_t j=0;j<inner;++j) {
      asm volatile("" ::: "memory"); // prevent hoisting repeated read-only passes
      sum+=action(i*inner+j);
    }
    const double us=std::chrono::duration<double,std::micro>(clock_type::now()-begin).count()/inner;
    observed+=sum;
    if(i>=20) times.push_back(us);
  }
  for(size_t i=0;i<times.size();++i) std::cout<<name<<','<<i<<','<<times[i]<<'\n';
}

template<class Map> void run(uint32_t entities,uint32_t attrs,size_t samples) {
  // Spare entities allow single and batched membership churn at fixed view size.
  const uint32_t batch=std::min<uint32_t>(entities,16);
  std::vector<box> boxes(size_t(entities+batch+1)*attrs);
  for(uint32_t e=1;e<=entities+batch;++e) for(uint32_t a=1;a<=attrs;++a)
    boxes[size_t(e)*attrs+a-1].value=a==1?mix(e)%1000000:key(e,a);
  auto value=[&](uint32_t e,uint32_t a){return &boxes[size_t(e)*attrs+a-1];};
  std::vector<uint32_t> insertion(entities); std::iota(insertion.begin(),insertion.end(),1);
  std::mt19937 rng(773); std::shuffle(insertion.begin(),insertion.end(),rng);
  auto fill=[&](Map& m){for(auto e:insertion) for(uint32_t a=1;a<=attrs;++a) m.set(key(e,a),value(e,a)); m.publish();};
  Map map; fill(map);
  uint64_t expected=0; for(uint32_t e=1;e<=entities;++e) for(uint32_t a=1;a<=attrs;++a) expected+=value(e,a)->value;
  const uint64_t ids_sum=uint64_t(entities)*(entities+1)/2;
  auto validate=[&] {
    uint64_t sum=0,count=0;
    map.each([&](auto k,ref v){require(map.get(k)==v,"lookup differs"); sum+=v->value; ++count;});
    require(sum==expected&&count==uint64_t(entities)*attrs,"fact contents differ");
    const auto ids=map.ids(); require(ids.size()==entities,"duplicate/missing entity");
    for(uint32_t i=0;i<entities;++i) require(ids[i]==i+1,"entity order differs");
    require(map.grouped_read(attrs)==expected+ids_sum,"grouped scan differs");
  };
  validate();
  const auto cached=map.ids();
  const size_t repeat=std::clamp<size_t>(1000/(size_t(entities)*attrs),1,100);
  std::cout<<"metric,sample,us\n";
  measure("lookup_cached_entities",samples,repeat,[&](size_t){
    uint64_t sum=0; for(auto e:cached) for(uint32_t a=1;a<=attrs;++a) sum+=map.get(key(e,a))->value;
    require(sum==expected,"read checksum"); return sum;
  });
  measure("entity_ids",samples,repeat,[&](size_t){
    auto ids=map.ids(); require(ids.size()==entities,"entity ID count"); return uint64_t(ids.front())+ids.back();
  });
  measure("grouped_scan",samples,repeat,[&](size_t){
    auto sum=map.grouped_read(attrs); require(sum==expected+ids_sum,"group checksum");return sum;
  });
  measure("ui_order_and_read",samples,repeat,[&](size_t){
    std::vector<std::pair<uint64_t,uint32_t>> order;
    for(auto e:map.ids(false)) order.emplace_back(map.get(key(e,1))->value,e);
    std::sort(order.begin(),order.end());
    uint64_t sum=0; for(auto [n,e]:order) for(uint32_t a=1;a<=attrs;++a) sum+=map.get(key(e,a))->value;
    require(sum==expected,"UI order read checksum"); return sum;
  });
  const box replacement{999};
  measure("replace_value",samples,100,[&](size_t i){
    auto v=i%2?value(1,attrs):&replacement;
    map.set(key(1,attrs),v); map.publish();
    return map.get(key(1,attrs))->value;
  });
  map.set(key(1,attrs),value(1,attrs)); map.publish(); validate();
  measure("replace_entity",samples,entities<100?20:1,[&](size_t i){
    const uint32_t removed=i%2?entities+1:1, added=i%2?1:entities+1;
    for(uint32_t a=1;a<=attrs;++a) map.erase(key(removed,a));
    for(uint32_t a=1;a<=attrs;++a) map.set(key(added,a),value(added,a));
    map.publish();
    require(!map.get(key(removed,1))&&map.get(key(added,1))==value(added,1),"churn differs");
    return map.get(key(added,1))->value;
  });
  // sample count is even, as are warmup and tiny-view inner batches.
  validate();
  // Two publications per measured cycle: create a new highest-ID entity, then
  // remove it. This exercises both fast paths without timing fixture resets.
  measure("append_remove_tail",samples,entities<100?20:1,[&](size_t){
    for(uint32_t a=1;a<=attrs;++a) map.set(key(entities+1,a),value(entities+1,a));
    map.publish();
    require(map.get(key(entities+1,1))==value(entities+1,1),"append missing");
    for(uint32_t a=1;a<=attrs;++a) map.erase(key(entities+1,a));
    map.publish(); require(!map.get(key(entities+1,1)),"suffix deletion missing");
    return uint64_t(entities);
  });
  validate();
  measure("replace_batch16",samples,entities<100?20:1,[&](size_t i){
    for(uint32_t e=1;e<=batch;++e) {
      const auto removed=i%2?entities+e:e, added=i%2?e:entities+e;
      for(uint32_t a=1;a<=attrs;++a) map.erase(key(removed,a));
      for(uint32_t a=1;a<=attrs;++a) map.set(key(added,a),value(added,a));
    }
    map.publish();
    const auto first=i%2?1:entities+1;
    require(map.get(key(first,1))==value(first,1),"batch replacement missing");
    return map.get(key(first,1))->value;
  });
  validate();
  measure("build_and_destroy",std::min<size_t>(samples,40),1,[&](size_t){
    Map fresh; fill(fresh); return fresh.get(key(entities,attrs))->value;
  });
  std::cerr<<"PASS checksum="<<observed<<" container_reserved_bytes="<<map.bytes()<<'\n';
}
int main(int argc,char** argv) {
  try {
    if(argc!=5) throw std::invalid_argument("layout entities attributes samples");
    const std::string layout=argv[1];
    auto n=std::stoul(argv[2]),a=std::stoul(argv[3]),samples=std::stoul(argv[4]);
    require(n&&a&&samples&&samples%2==0,"dimensions must be positive, samples even");
    if(layout=="nested") run<nested>(n,a,samples);
    else if(layout=="flat") run<flat<false>>(n,a,samples);
    else if(layout=="sorted_dense") run<flat<true>>(n,a,samples);
    else if(layout=="sorted_merge") run<merged_dense>(n,a,samples);
    else if(layout=="wide_slab") run<wide>(n,a,samples);
    else throw std::invalid_argument("unknown layout");
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
