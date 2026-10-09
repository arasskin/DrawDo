#include "datafriend/engine.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace datafriend;
using clock_type=std::chrono::steady_clock;
int main(int argc,char** argv) {
  const size_t entities=argc>1?std::stoul(argv[1]):10000, fanout=argc>2?std::stoul(argv[2]):10;
  const size_t different=argc>3?std::stoul(argv[3]):0;
  const size_t samples=1000, warmup=100;
  char path[]="/tmp/datafriend-profile-XXXXXX"; if(!mkdtemp(path)) return 1;
  {
    engine db(path,false); auto all=db.subscribe({{},{},true}); edit seed;
    for(uint64_t id=1;id<=entities;++id) {seed.add.push_back({id,"group",std::to_string(id%100)});seed.add.push_back({id,"done","false"});seed.add.push_back({id,"title","task"});}
    db.modify(std::move(seed)); db.unsubscribe(all);
    std::vector<uint64_t> views;
    for(size_t i=0;i<fanout;++i) views.push_back(db.subscribe({{{{0,{}},"group",{-1,"1"}},{{0,{}},"done",{1,{}}},{{0,{}},"title",{2,{}}}},{1,2}}));
    // Distinct group constants select equally sized, disjoint entity sets.
    // They still depend on done, so the same edit invalidates all of them.
    if(different>98) throw std::invalid_argument("at most 98 distinct groups");
    for(size_t i=0;i<different;++i) views.push_back(db.subscribe({{{{0,{}},"group",{-1,std::to_string(i+2)}},{{0,{}},"done",{1,{}}},{{0,{}},"title",{2,{}}}},{1,2}}));
    struct field { const char* name; uint64_t edit_profile::*value; };
    const field fields[]={{"working_view",&edit_profile::working_ns},{"reconcile",&edit_profile::reconcile_ns},
      {"read_entity",&edit_profile::read_ns},{"stage_aelmdb",&edit_profile::stage_ns},{"indexes_subscribers",&edit_profile::indexes_ns},
      {"queries",&edit_profile::queries_ns},{"commit_nosync",&edit_profile::commit_ns},{"publish_roots",&edit_profile::publish_ns},
      {"owner_total",&edit_profile::owner_ns}};
    std::vector<std::vector<double>> times(std::size(fields)+2);
    edit_profile last;
    for(size_t i=0;i<samples+warmup;++i) {
      auto begin=clock_type::now(); db.modify({{},{{1,"done",i%2?"false":"true"}}});
      auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now()-begin).count();
      last=db.last_profile();
      if(i<warmup) continue;
      for(size_t j=0;j<std::size(fields);++j) times[j].push_back(double(last.*fields[j].value)/1000);
      times[std::size(fields)].push_back(double(ns)/1000);
      times[std::size(fields)+1].push_back((double(ns)-last.owner_ns)/1000);
    }
    std::cout<<"entities,identical_subscribers,additional_queries,query_evaluations,phase,mean_us,p50_us,p99_us\n";
    auto report=[&](const char* name,std::vector<double>& v) {
      double sum=0;for(auto x:v)sum+=x;std::sort(v.begin(),v.end());
      std::cout<<entities<<','<<fanout<<','<<different<<','<<db.last_recomputed()<<','<<name<<','<<sum/v.size()<<','<<v[v.size()/2]<<','<<v[v.size()*99/100]<<'\n';
    };
    for(size_t j=0;j<std::size(fields);++j) report(fields[j].name,times[j]);
    report("call_total",times[std::size(fields)]); report("caller_handoff",times[std::size(fields)+1]);
    std::vector<double> control;
    for(size_t i=0;i<samples;++i) {auto begin=clock_type::now(); (void)db.revision();control.push_back(std::chrono::duration<double,std::micro>(clock_type::now()-begin).count());}
    report("empty_roundtrip",control);
    std::cerr<<"entities="<<entities<<" subscribers="<<fanout<<" additional_queries="<<different<<" query_evaluations="<<db.last_recomputed()<<" candidates="<<last.candidates<<" emitted="<<last.emitted<<" previous_entries="<<last.previous_entries<<" query_tree_writes="<<last.query_tree_writes<<'\n';
  }
  std::filesystem::remove_all(path);
}
