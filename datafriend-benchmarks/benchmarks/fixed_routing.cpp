#include "datafriend/engine.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace datafriend;

// Tight native title-edit loop. Subscriber count varies independently of the
// 1,000 stored entities. No Flutter, FFI, decoding, or artificial frame pauses.
int main(int argc,char** argv) {
  try {
    const size_t subscribers=argc>1?std::stoul(argv[1]):12;
    const size_t expected=argc>2?std::stoul(argv[2]):1;
    if(!subscribers||subscribers>1000) throw std::invalid_argument("subscribers must be 1..1000");
    char path[]="/tmp/datafriend-routing-XXXXXX";
    if(!mkdtemp(path)) throw std::runtime_error("mkdtemp failed");
    struct cleanup { const char* path; ~cleanup() {std::filesystem::remove_all(path);} } clean{path};
    engine db(path,durability::asynchronous);
    edit seed;
    for(uint32_t id=1;id<=1000;++id) {
      seed.add.push_back({id,"title","task"});
      seed.add.push_back({id,"done","false"});
    }
    db.modify(std::move(seed));
    uint64_t first=0;
    for(uint32_t id=1;id<=subscribers;++id) {
      term entity{-1,entity_value(id)};
      auto view=db.subscribe({{{entity,"title",{0,{}}},{entity,"done",{1,{}}}},{0,1}});
      if(id==1) first=view;
    }
    std::vector<double> timings;
    for(size_t i=0;i<3500;++i) {
      const auto start=std::chrono::steady_clock::now();
      auto changed=db.modify({{},{{1,"title",i%2?"task":"edit"}}});
      const auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
      if(changed!=std::vector<uint64_t>{first}) throw std::runtime_error("wrong notifications");
      if(i>=500) timings.push_back(us);
    }
    if(db.last_recomputed()!=expected) throw std::runtime_error("wrong routed query count");
    if(db.result(first).lookup(1,"title")->value->value!="task") throw std::runtime_error("wrong final result");
    std::sort(timings.begin(),timings.end());
    std::cout<<"subscribers,evaluated,samples,p50_us,p95_us,max_us\n"
             <<subscribers<<','<<expected<<','<<timings.size()<<','<<timings[timings.size()/2]<<','
             <<timings[timings.size()*95/100]<<','<<timings.back()<<'\n';
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
