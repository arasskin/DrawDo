#include "datafriend/engine.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace datafriend;

int main(int argc,char** argv) {
  try {
    const std::string shape=argc>1?argv[1]:"task5";
    const bool batch=argc>2&&std::string(argv[2])=="two_fields";
    if(shape!="task5"&&shape!="wide32"&&shape!="blob32k") throw std::invalid_argument("shape");
    char path[]="/tmp/datafriend-fact-edits-XXXXXX";
    if(!mkdtemp(path)) throw std::runtime_error("mkdtemp");
    struct cleanup { const char* path; ~cleanup(){std::filesystem::remove_all(path);} } clean{path};
    engine db(path,durability::asynchronous); edit seed;
    for(uint32_t id=1;id<=1000;++id) {
      for(const auto& [a,v]:entity_record{{"kind","task"},{"id",std::to_string(id)},
          {"title","task"},{"done","false"},{"order",std::to_string(id)}}) seed.add.push_back({id,a,v});
      if(shape=="wide32") for(int a=0;a<27;++a) seed.add.push_back({id,"field"+std::to_string(a),"unchanged field value"});
      if(shape=="blob32k") seed.add.push_back({id,"blob",bytes(32768,'x')});
    }
    db.modify(std::move(seed));
    // Stabilize initial bulk flushing before measuring incremental edits.
    db.flush();
    const auto seed_bytes=std::filesystem::file_size(std::string(path)+"/data.mdb");
    uint64_t first=0;
    for(uint32_t id=1;id<=12;++id) {
      term e{-1,entity_value(id)};
      auto v=db.subscribe({{{e,"title",{0,{}}},{e,"done",{1,{}}}},{0,1}});
      if(id==1) first=v;
    }
    auto counter=db.subscribe({{{{0,{}},"kind",{-1,"task"}},{{0,{}},"done",{1,{}}}},{1}});
    auto list=db.subscribe({{{{0,{}},"kind",{-1,"task"}},{{0,{}},"order",{1,{}}}},{1}});
    std::vector<double> times;
    for(size_t i=0;i<2500;++i) {
      auto begin=std::chrono::steady_clock::now();
      edit delta; delta.add.push_back({1,"done",i%2?"false":"true"});
      if(batch) delta.add.push_back({1,"title",i%2?"task":"edit"});
      const auto changed=db.modify(std::move(delta));
      const auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
      if(changed!=std::vector<uint64_t>{first,counter}) throw std::runtime_error("notification mismatch");
      if(i>=500) times.push_back(us);
    }
    if(db.last_recomputed()!=2||db.result(counter).size()!=1000||db.result(list).size()!=1000)
      throw std::runtime_error("query result/count mismatch");
    auto final=db.export_entities();
    if(final.size()!=1000) throw std::runtime_error("lost entities");
    for(const auto& [id,record]:final) {
      const auto values=decode_entity(id,record);
      if(values.at("done")!="false"||values.at("title")!="task"||values.at("id")!=std::to_string(id))
        throw std::runtime_error("final entity mismatch");
      if(values.at("kind")!="task"||values.at("order")!=std::to_string(id)) throw std::runtime_error("other fields changed");
      if(shape=="wide32"&&values.size()!=32) throw std::runtime_error("wide entity changed");
      if(shape=="wide32") for(int a=0;a<27;++a)
        if(values.at("field"+std::to_string(a))!="unchanged field value") throw std::runtime_error("field changed");
      if(shape=="blob32k"&&(values.size()!=6||values.at("blob")!=bytes(32768,'x'))) throw std::runtime_error("blob changed");
      if(shape=="task5"&&values.size()!=5) throw std::runtime_error("task fields changed");
    }
    std::sort(times.begin(),times.end());
    std::cout<<"shape,workload,samples,p50_us,p95_us,max_us,seed_file_bytes\n"
             <<shape<<','<<(batch?"two_fields":"toggle")<<','<<times.size()<<','
             <<times[times.size()/2]<<','<<times[times.size()*95/100]<<','<<times.back()<<','<<seed_bytes<<'\n';
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
