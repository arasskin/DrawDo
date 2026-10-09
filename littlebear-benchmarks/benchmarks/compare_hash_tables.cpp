#define XXH_INLINE_ALL
#include "reference/hash_table.h"
#include "namespaces.h"
#include <ankerl/unordered_dense.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

namespace ht = legacy_hash_table;
using Key = ht::key;
using Value = ht::value;
void require(bool ok, std::source_location loc=std::source_location::current()) {
  if (!ok) throw std::runtime_error("validation failed at line " + std::to_string(loc.line()));
}
struct Equal { bool operator()(Key a, Key b) const { return ht::operator==(a,b); } };
// Inputs are already uniformly distributed 128-bit fingerprints. Reusing one
// half avoids charging only dense for hashing an already-hashed namespace.
struct Prehashed {
  using is_avalanching = void;
  uint64_t operator()(Key k) const { return k.low64; }
};
struct Rehashed {
  using is_avalanching = void;
  uint64_t operator()(Key k) const { return XXH3_64bits(&k, sizeof(k)); }
};
struct Current {
  ht::two_way t = ht::create();
  Current() { require(!ht::bad(&t)); }
  ~Current() { ht::destroy(&t); }
  bool insert(Key k, Value v) { return ht::unsafe_insert(&t,k,v); }
  void update(Key k, Value v) { require(ht::associate(&t,k,v)); }
  Value get(Key k) { auto p=ht::get(&t,k); return p ? *p : Value{}; }
  void erase(Key k) { require(ht::dissociate(&t,k)); }
  size_t size() const { return ht::count(&t); }
  size_t bytes() const { return ht::memory_usage(&t); }
  size_t slots() const { return t.bucket_count*ht::BUCKET_SIZE; }
};
struct Integrated {
  hash_table::directory t = hash_table::create();
  Integrated() { require(!hash_table::bad(&t)); }
  bool insert(Key k, Value v) { return hash_table::unsafe_insert(&t,k,v); }
  void update(Key k, Value v) { require(hash_table::associate(&t,k,v)); }
  Value get(Key k) { auto p=hash_table::get(&t,k); return p ? *p : Value{}; }
  void erase(Key k) { require(hash_table::dissociate(&t,k)); }
  size_t size() const { return hash_table::count(&t); }
  size_t bytes() const { return hash_table::memory_usage(&t); }
  size_t slots() const { return t.entries.bucket_count(); }
};
template<class Hash> struct Dense {
  using Map=ankerl::unordered_dense::map<Key,Value,Hash,Equal>;
  Map t;
  bool insert(Key k, Value v) { return t.emplace(k,v).second; }
  void update(Key k, Value v) { t.insert_or_assign(k,v); }
  Value get(Key k) { auto it=t.find(k); return it==t.end() ? Value{} : it->second; }
  void erase(Key k) { require(t.erase(k)==1); }
  size_t size() const { return t.size(); }
  size_t bytes() const { return sizeof(Map)+t.index_bytes()+t.values().capacity()*sizeof(typename Map::value_type); }
  size_t slots() const { return t.bucket_count(); }
};
using Clock=std::chrono::steady_clock;
template<class F> double timed(size_t n, F f) {
  auto start=Clock::now(); f();
  return std::chrono::duration<double,std::nano>(Clock::now()-start).count()/n;
}
Value value(size_t i) { Value v{}; v.rank=uint32_t(i+1); return v; }
bool same(Value a, Value b) {
  return a.root==b.root && a.node_allocator==b.node_allocator && a.rank==b.rank;
}
template<class Map> void run(const char* engine,size_t n,uint64_t seed) {
  std::mt19937_64 random(seed);
  std::vector<Key> keys(2*n);
  for (auto& k:keys) k={random(),random()};
  // Verify unique inputs; use ordinary fingerprint keys in the timed workload.
  auto sorted=keys;
  std::sort(sorted.begin(),sorted.end(),[](Key a,Key b) {
    return a.high64!=b.high64 ? a.high64<b.high64 : a.low64<b.low64;
  });
  for(size_t i=0;i<sorted.size();++i) {
    require(!Equal{}(sorted[i],ht::EMPTY));
    if(i) require(!Equal{}(sorted[i-1],sorted[i]));
  }
  std::vector<size_t> order(n);
  std::iota(order.begin(),order.end(),0);
  std::shuffle(order.begin(),order.end(),random);
  // Fixed trace generated before timing: uniform random query positions.
  size_t queries=std::max(size_t(1<<20),n*8);
  std::vector<size_t> trace(queries);
  uint64_t expected=0;
  for(auto& i:trace) { i=random()%n; expected+=i+1; }
  Map map;
  size_t inserted=0;
  double insert_ns=timed(n,[&] {
    for(size_t i=0;i<n;++i) { if(!map.insert(keys[i],value(i))) break; ++inserted; }
  });
  if(inserted!=n) {
    require(map.size()==inserted);
    for(size_t i=0;i<inserted;++i) require(same(map.get(keys[i]),value(i)));
    require(map.get(keys[inserted]).rank==0);
    std::printf("{\"engine\":\"%s\",\"count\":%zu,\"seed\":%llu,\"status\":\"capacity_failure\",\"inserted\":%zu,\"bytes\":%zu}\n",engine,n,(unsigned long long)seed,inserted,map.bytes());
    return;
  }
  size_t build_bytes=map.bytes(), build_slots=map.slots();
  require(map.size()==n);
  for(size_t i=0;i<n;++i) require(same(map.get(keys[i]),value(i)));
  // Warm the table and code before lookup timing.
  uint64_t warm=0;
  for(auto i:order) warm+=map.get(keys[i]).rank;
  require(warm==n*(n+1)/2);
  uint64_t hit_sum=0,miss_sum=0;
  double hit_ns=timed(queries,[&] { for(auto i:trace) hit_sum+=map.get(keys[i]).rank; });
  require(hit_sum==expected);
  double miss_ns=timed(queries,[&] { for(auto i:trace) miss_sum+=map.get(keys[n+i]).rank==0; });
  require(miss_sum==queries);
  double update_ns=timed(queries,[&] { for(auto i:trace) map.update(keys[i],value(n+i)); });
  std::vector<bool> updated(n);
  for(auto i:trace) updated[i]=true;
  for(size_t i=0;i<n;++i) require(same(map.get(keys[i]),value((updated[i]?n:0)+i)));
  // Replace every key by a distinct absent key, then reverse; 8 full passes.
  // Timings are per pair (erase + insert), not per individual operation.
  constexpr size_t passes=8;
  size_t failed_pass=passes,failed_pos=0;
  double churn_ns=timed(n*passes,[&] {
    for(size_t p=0;p<passes;++p) for(size_t j=0;j<n;++j) {
      auto i=order[j];
      map.erase(keys[(p%2)*n+i]);
      if(!map.insert(keys[((p+1)%2)*n+i],value(i))) {
        failed_pass=p; failed_pos=j; return;
      }
    }
  });
  if(failed_pass!=passes) {
    require(map.size()==n-1);
    for(size_t j=0;j<n;++j) {
      size_t i=order[j];
      if(j==failed_pos) {
        require(map.get(keys[i]).rank==0 && map.get(keys[n+i]).rank==0);
        continue;
      }
      size_t active=(failed_pass+(j<failed_pos))%2;
      size_t token=(failed_pass==0 && j>failed_pos && updated[i])?n+i:i;
      require(same(map.get(keys[active*n+i]),value(token)));
      require(map.get(keys[(1-active)*n+i]).rank==0);
    }
    std::printf("{\"engine\":\"%s\",\"count\":%zu,\"seed\":%llu,\"status\":\"churn_capacity_failure\",\"completed_pairs\":%zu,\"build_bytes\":%zu,\"bytes\":%zu}\n",engine,n,(unsigned long long)seed,failed_pass*n+failed_pos,build_bytes,map.bytes());
    return;
  }
  require(map.size()==n);
  for(size_t i=0;i<n;++i) {
    require(same(map.get(keys[i]),value(i)));
    require(map.get(keys[n+i]).rank==0);
  }
  size_t churn_bytes=map.bytes();
  double erase_ns=timed(n,[&] { for(auto i:order) map.erase(keys[i]); });
  require(map.size()==0);
  for(auto k:keys) require(map.get(k).rank==0);
  std::printf("{\"engine\":\"%s\",\"count\":%zu,\"seed\":%llu,\"status\":\"ok\",\"queries\":%zu,\"insert_ns\":%.6f,\"hit_ns\":%.6f,\"miss_ns\":%.6f,\"update_ns\":%.6f,\"churn_pair_ns\":%.6f,\"erase_ns\":%.6f,\"build_bytes\":%zu,\"churn_bytes\":%zu,\"empty_bytes\":%zu,\"slots\":%zu,\"checksum\":%llu,\"key_bytes\":%zu,\"value_bytes\":%zu}\n",engine,n,(unsigned long long)seed,queries,insert_ns,hit_ns,miss_ns,update_ns,churn_ns,erase_ns,build_bytes,churn_bytes,map.bytes(),build_slots,(unsigned long long)hit_sum,sizeof(Key),sizeof(Value));
}
int main(int argc,char** argv) {
  try {
    require(argc==4);
    size_t n=std::stoull(argv[2]);
    require(n>0 && n<(UINT32_MAX/2));
    uint64_t seed=std::stoull(argv[3]);
    std::string engine=argv[1];
    if(engine=="current") run<Current>(argv[1],n,seed);
    else if(engine=="integrated") run<Integrated>(argv[1],n,seed);
    else if(engine=="dense_prehashed") run<Dense<Prehashed>>(argv[1],n,seed);
    else if(engine=="dense_xxhash") run<Dense<Rehashed>>(argv[1],n,seed);
    else throw std::runtime_error("unknown engine");
  } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
