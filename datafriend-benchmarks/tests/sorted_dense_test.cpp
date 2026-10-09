#include "datafriend/sorted_dense_map.h"
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>

void require(bool ok,const char* msg) {if(!ok) throw std::runtime_error(msg);}
int main() {
  try {
    datafriend::sorted_dense_map<std::shared_ptr<const uint64_t>> map;
    std::map<uint64_t,uint64_t> model;
    auto set=[&](uint64_t k,uint64_t v){map.set(k,std::make_shared<const uint64_t>(v));model[k]=v;};
    auto erase=[&](uint64_t k){map.erase(k);model.erase(k);};
    auto check=[&] {
      for(const auto& [k,v]:model) require(map.get(k)&&**map.get(k)==v,"pending lookup");
      map.publish(); const auto& values=map.values();
      require(values.size()==model.size(),"size");
      size_t i=0;
      for(auto [k,v]:model) {
        require(values[i].first==k&&*values[i].second==v,"ordered merge contents");
        require(map.get(k)&&**map.get(k)==v,"hash index"); ++i;
      }
    };
    set(0,1);set(100,2);set(UINT64_MAX,3);check();
    require(map.stats().merges==0,"initial append rebuilt");
    erase(UINT64_MAX);erase(100);check();
    require(map.stats().merges==0,"suffix removal rebuilt");
    set(20,4);set(10,5);check();
    require(map.stats().merges==0,"unsorted append batch rebuilt");
    const auto tail=map.stats().tail_batches;
    erase(10);set(10,6);set(15,7);erase(15);check();
    require(map.stats().merges==0&&map.stats().tail_batches==tail,"canceled membership batch rebuilt");
    erase(0);set(5,8);check();require(map.stats().merges==1,"middle merge missing");
    // Deleted references must be released after publication.
    auto object=std::make_shared<const uint64_t>(999);
    std::weak_ptr<const uint64_t> weak=object;
    map.set(7,object);model[7]=999;object.reset();check();erase(7);check();
    require(weak.expired(),"deleted value retained");
    std::mt19937_64 random(713);
    for(size_t batch=0;batch<10000;++batch) {
      for(size_t i=0,n=random()%30+1;i<n;++i) {
        const auto r=random(); const uint64_t k=r%29==0?UINT64_MAX:r%257;
        if(random()%3) set(k,random());else erase(k);
        auto got=map.get(k); auto expected=model.find(k);
        require((got!=nullptr)==(expected!=model.end()),"pending deletion");
        if(got) require(**got==expected->second,"last operation wins");
      }
      check();
    }
    for(const auto& [k,v]:model) map.erase(k);
    model.clear();check();require(map.values().empty(),"clear");
    // Retain a large pending table, then repeatedly clear sparse tail batches.
    for(uint64_t k=0;k<4096;++k) set(k,k);
    check();
    for(size_t i=0;i<100;++i) {set(8192,i);check();erase(8192);check();}
    std::cout<<"PASS: sorted merge, append/suffix, canceled edits, extreme keys, ownership, randomized batches\n";
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
