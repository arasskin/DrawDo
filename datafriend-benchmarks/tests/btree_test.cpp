#include "../benchmarks/btree_index.h"
#include "../benchmarks/reconcile.h"
#include <map>
#include <random>
#include <iostream>
using namespace experiment;

template<class Index> void test() {
  typename Index::pool_type pool;
  Index a(pool), b(pool);
  std::map<uint64_t,record> model;
  std::mt19937_64 rng(35);
  auto check=[&] {
    a.validate(); b.validate();
    std::vector<record> expected;
    for(auto [k,r]:model) { require(a.get(k)==r,"Btree lookup"); expected.push_back(r); }
    require(a.list({},SIZE_MAX)==expected,"Btree enumeration");
    require(a.fingerprint()==Index::expected_hash(expected),"Btree root hash");
    for(unsigned i=0;i<8;++i) {
      key_range range{rng(),rng()};
      if(i==0) range={UINT64_MAX,{}};
      expected.clear();
      for(auto [k,r]:model) if(range.contains(k)) expected.push_back(r);
      require(a.range_hash(range)==Index::expected_hash(expected),"Btree range hash");
      expected.resize(std::min<size_t>(7,expected.size()));
      require(a.list(range,7)==expected,"Btree bounded list");
    }
  };
  for(auto k:{uint64_t(0),UINT64_MAX}) { auto r=make_record(k,7);a.set(r);model[k]=r; }
  b.set(make_record(0,8)); b.set(make_record(UINT64_MAX,9));
  for(size_t i=0;i<7000;++i) {
    auto k=permute(rng()%1200);
    if(rng()%4) { auto r=make_record(k,rng());a.set(r);model[k]=r; }
    else require(a.erase(k)==bool(model.erase(k)),"Btree removal");
    if(i%97==0) check();
  }
  check();
  require(b.get(0)==make_record(0,8),"shared pool namespaces interfered");
  auto result=reconciliation::run(a,b);
  std::set<uint64_t> expected_diff;
  for(auto r:a.list({},SIZE_MAX)) if(b.get(r.key)!=r) expected_diff.insert(r.key);
  for(auto r:b.list({},SIZE_MAX)) if(a.get(r.key)!=r) expected_diff.insert(r.key);
  require(result.differences==expected_diff,"Btree reconciliation");
  for(auto k:result.differences) { if(auto r=a.get(k)) b.set(*r);else b.erase(k); }
  require(a.list({},SIZE_MAX)==b.list({},SIZE_MAX),"Btree convergence");
  for(auto [k,r]:model) require(a.erase(k),"Btree clear");
  require(a.size()==0,"Btree not empty");a.validate();
  require(reconciliation::run(a,b).differences.size()==model.size(),"empty client reconciliation");
  for(auto r:b.list({},SIZE_MAX)) b.erase(r.key);
  require(pool.nodes.empty(),"Btree retained nodes");
  require(reconciliation::run(a,b).differences.empty(),"empty peers");
}
int main() {
  try {
    test<btree_original>();test<btree_xor>();
    std::cout<<"PASS: upstream and XOR B-trees, model mutations, shared namespaces, ranges, reconciliation, reclamation\n";
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
