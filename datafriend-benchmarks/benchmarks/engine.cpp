#include "datafriend/engine.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace datafriend;
int main(int argc,char** argv) {
  const size_t entities=argc>1?std::stoul(argv[1]):10000, fanout=argc>2?std::stoul(argv[2]):10;
  const size_t different=argc>4?std::stoul(argv[4]):0;
  const bool durable=argc>3?std::stoi(argv[3]):true;
  char path[]="/tmp/datafriend-bench-XXXXXX"; if(!mkdtemp(path)) return 1;
  {
    engine db(path,durable); auto all=db.subscribe({{},{},true}); edit seed;
    for(uint64_t id=1;id<=entities;++id) {seed.add.push_back({id,"group",std::to_string(id%100)});seed.add.push_back({id,"done","false"});seed.add.push_back({id,"title","task"});}
    db.modify(std::move(seed)); db.unsubscribe(all);
    std::vector<uint64_t> views;
    for(size_t i=0;i<fanout;++i) views.push_back(db.subscribe({{{{0,{}},"group",{-1,"1"}},{{0,{}},"done",{1,{}}},{{0,{}},"title",{2,{}}}},{1,2}}));
    // Distinct group constants select equally sized, disjoint entity sets.
    // They still depend on done, so the same edit invalidates all of them.
    if(different>98) throw std::invalid_argument("at most 98 distinct groups");
    for(size_t i=0;i<different;++i) views.push_back(db.subscribe({{{{0,{}},"group",{-1,std::to_string(i+2)}},{{0,{}},"done",{1,{}}},{{0,{}},"title",{2,{}}}},{1,2}}));
    std::vector<double> times;
    for(size_t i=0;i<1100;++i) {
      auto begin=std::chrono::steady_clock::now();
      db.modify({{},{{1,"done",i%2?"false":"true"}}});
      // Includes publication, owner-thread roundtrip and old-root reclamation.
      auto elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
      if(i>=100) times.push_back(elapsed);
    }
    std::sort(times.begin(),times.end());
    std::cout<<entities<<','<<fanout<<','<<different<<','<<db.last_recomputed()<<','<<durable<<','<<times[times.size()/2]<<','<<times[times.size()*95/100]<<','<<times[times.size()*99/100]<<','<<times.back()<<'\n';
  }
  std::filesystem::remove_all(path);
}
