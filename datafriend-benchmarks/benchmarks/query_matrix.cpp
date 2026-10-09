// Host-only C ABI harness. One executable loads either old or new library;
// both run the same workload, validation, serialization and timing code.
#include "datafriend/engine.h"
#include "datafriend/codec.h"
#include "datafriend/c_api.h"
#include <dlfcn.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <unistd.h>
using namespace datafriend;
using records = std::map<uint64_t,entity_record>;
using facts = std::map<std::pair<uint64_t,std::string>,bytes>;
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
bytes ref(uint64_t e) { writer w; w.u8(6); w.u64(e); return w.data; }
term var(int i) { return {i,{}}; }
term lit(bytes s) { return {-1,std::move(s)}; }
struct client {
  void* library=nullptr; void* db=nullptr;
  decltype(&df_open) open;
  decltype(&df_close) close;
  decltype(&df_call) call;
  decltype(&df_free) free;
  std::string path;
  template<class F> F symbol(const char* name) {
    auto p=dlsym(library,name); if(!p) throw std::runtime_error(dlerror()); return reinterpret_cast<F>(p);
  }
  explicit client(const char* name) {
    library=dlopen(name,RTLD_NOW|RTLD_LOCAL); if(!library) throw std::runtime_error(dlerror());
    open=symbol<decltype(open)>("df_open"); close=symbol<decltype(close)>("df_close");
    call=symbol<decltype(call)>("df_call"); free=symbol<decltype(free)>("df_free");
    char temp[]="/tmp/datafriend-query-matrix-XXXXXX"; require(mkdtemp(temp),"temporary directory"); path=temp;
    df_buffer error{}; db=open(reinterpret_cast<const uint8_t*>(path.data()),path.size(),0,&error);
    auto message=take(error); if(!db) throw std::runtime_error(message);
  }
  ~client() { if(db) close(db); if(library) dlclose(library); std::filesystem::remove_all(path); }
  bytes take(df_buffer b) { bytes out; if(b.size) out.assign(reinterpret_cast<const char*>(b.data),b.size); free(b.data); return out; }
  bytes send(const writer& w) {
    df_buffer reply{}; auto status=call(db,reinterpret_cast<const uint8_t*>(w.data.data()),w.data.size(),&reply);
    auto result=take(reply); if(status) throw std::runtime_error(result); return result;
  }
  static writer request(uint8_t op) { writer w; w.u8(1); w.u8(op); return w; }
  static writer subscribe_request(const query& q) {
    auto w=request(1); w.u8(q.all); w.u32(uint32_t(q.clauses.size()));
    auto term=[&](const datafriend::term& t) { w.u8(t.variable<0?0:1); if(t.variable<0) w.blob(t.constant); else w.u8(uint8_t(t.variable)); };
    for(const auto& c:q.clauses) { term(c.entity); w.blob(c.attribute); term(c.value); }
    w.u32(uint32_t(q.projection.size())); for(auto i:q.projection) w.u32(i); return w;
  }
  uint64_t subscribe(const query& q) { auto result=send(subscribe_request(q)); reader r{result}; return r.u64(); }
  void unsubscribe(uint64_t id) { auto w=request(4); w.u64(id); send(w); }
  static writer edit_request(const edit& e) {
    auto w=request(10);
    for(auto* list:{&e.remove,&e.add}) {
      w.u32(uint32_t(list->size()));
      for(const auto& t:*list) { w.u64(t.entity); w.blob(t.attribute); w.blob(t.value); }
    }
    return w;
  }
  facts read(uint64_t id) {
    auto w=request(2); w.u64(id); auto result=send(w); reader r{result}; facts out;
    auto n=r.u32();
    for(uint32_t i=0;i<n;++i) { auto e=r.u64(); auto a=r.blob(); auto v=r.blob(); require(out.emplace(std::make_pair(e,a),v).second,"duplicate output fact"); }
    r.end(); return out;
  }
};
constexpr uint64_t owner_base=1000000;
entity_record task(uint64_t id) {
  entity_record row={{"kind","task"},{"title","task "+std::to_string(id)},
    {"done","false"},{"group",std::to_string(id%100)},{"skew",id%2?"cold":"hot"},
    {"owner",ref(owner_base+id%100)},{"team",std::to_string(id%10)},{"id",std::to_string(id)}};
  for(int a=0;a<8;++a) row["field"+std::to_string(a)]="value"+std::to_string(a);
  return row;
}
query make_query(const std::string& shape,int group=0) {
  query q;
  auto add=[&](term e,std::string a,term v,bool project) {
    if(project) q.projection.push_back(uint32_t(q.clauses.size()));
    q.clauses.push_back({std::move(e),std::move(a),std::move(v)});
  };
  if(shape=="wide") {
    int i=1; for(const auto& [a,v]:task(1)) add(var(0),a,var(i++),true);
  } else if(shape=="rare_first" || shape=="rare_last" || shape=="broad") {
    const auto attribute=shape=="broad"?"skew":"group";
    const auto value=shape=="broad"?std::string("hot"):std::to_string(group);
    if(shape=="rare_first") add(var(0),attribute,lit(value),false);
    add(var(0),"title",var(1),true); add(var(0),"done",var(2),true);
    add(var(0),"field0",var(3),false); // required, unused value
    if(shape!="rare_first") add(var(0),attribute,lit(value),false);
  } else if(shape=="forward" || shape=="reverse") {
    if(shape=="reverse") add(var(1),"region",lit("rare"),false);
    add(var(0),"owner",var(1),false);
    if(shape=="forward") add(var(1),"region",lit("rare"),false);
    add(var(0),"title",var(2),true);
  } else if(shape=="chain") {
    add(var(0),"owner",var(1),false); add(var(1),"next",var(2),false);
    add(var(2),"region",lit("rare"),false); add(var(0),"title",var(3),true);
  } else if(shape=="value_join") {
    add(var(0),"team",var(2),false); add(var(1),"owner_team",var(2),false);
    add(var(1),"region",lit("rare"),false); add(var(0),"title",var(3),true);
  } else if(shape=="equality") {
    add(var(0),"group",var(1),false); add(var(0),"team",var(1),false);
    add(var(0),"title",var(2),true);
  } else if(shape=="disconnected") {
    add(var(0),"title",var(1),true); add(var(2),"region",lit("rare"),false);
  } else if(shape=="empty") {
    add(var(0),"title",var(1),true); add(var(0),"absent",var(2),false);
  } else throw std::runtime_error("unknown shape");
  return q;
}
// Independent workload-specific oracle uses plain entity records, never engine
// indexes or its planner. All shapes have exactly this logical interpretation.
facts expected(const records& world,const std::string& shape,int group=0) {
  facts result; std::set<std::string> rare_teams;
  bool rare_exists=false;
  for(const auto& [e,row]:world) if(row.contains("region")&&row.at("region")=="rare") {
    rare_exists=true; rare_teams.insert(row.at("owner_team"));
  }
  for(const auto& [e,row]:world) {
    if(!row.contains("title")) continue;
    bool match=false;
    if(shape=="wide") match=row.size()>=16;
    else if(shape=="rare_first"||shape=="rare_last") match=row.at("group")==std::to_string(group);
    else if(shape=="broad") match=row.at("skew")=="hot";
    else if(shape=="forward"||shape=="reverse"||shape=="chain") {
      reader r{row.at("owner")}; r.u8(); auto owner=r.u64();
      if(shape=="chain") { reader next{world.at(owner).at("next")}; next.u8(); owner=next.u64(); }
      match=world.at(owner).at("region")=="rare";
    } else if(shape=="value_join") match=rare_teams.contains(row.at("team"));
    else if(shape=="equality") match=row.at("group")==row.at("team");
    else if(shape=="disconnected") match=rare_exists;
    if(!match) continue;
    if(shape=="wide") {
      for(const auto& [a,v]:task(e)) result[{e,a}]=row.at(a);
    } else {
      result[{e,"title"}]=row.at("title");
      if(shape=="rare_first"||shape=="rare_last"||shape=="broad") result[{e,"done"}]=row.at("done");
    }
  }
  return result;
}
void apply_edit(records& world,const edit& delta) {
  for(const auto& t:delta.remove) {
    auto row=world.find(t.entity); if(row==world.end()) continue;
    auto a=row->second.find(t.attribute);
    if(a!=row->second.end()&&a->second==t.value) row->second.erase(a);
    if(row->second.empty()) world.erase(row);
  }
  for(const auto& t:delta.add) world[t.entity][t.attribute]=t.value;
}
uint64_t digest(const facts& values) {
  uint64_t h=14695981039346656037ULL;
  for(const auto& [key,value]:values) {
    writer w; w.u64(key.first); w.blob(key.second); w.blob(value);
    for(unsigned char b:w.data) { h^=b; h*=1099511628211ULL; }
  }
  return h;
}
int main(int argc,char** argv) {
  try {
    require(argc==7,"library workload entities samples warmup seed");
    client db(argv[1]); const std::string workload=argv[2];
    const size_t n=std::stoul(argv[3]), samples=std::stoul(argv[4]), warmup=std::stoul(argv[5]);
    require(n>=32&&n<owner_base&&samples>0,"invalid dimensions");
    std::mt19937 random(std::stoul(argv[6]));
    const bool subscription=workload.starts_with("subscribe_");
    std::string shape=subscription?workload.substr(10):workload.starts_with("edit_")?workload.substr(5):"rare_last";
    if(workload=="batch32"||workload=="delete_reinsert"||workload=="unrelated"||workload=="noop") shape="wide";
    if(workload=="remote") shape="chain";
    records world;
    for(uint64_t id=1;id<=n;++id) world[id]=task(id);
    for(uint64_t i=0;i<100;++i) world[owner_base+i]={{"region",i==0?"rare":"common"},
      {"owner_team",std::to_string(i%10)},{"next",ref(owner_base+(i+1)%100)}};
    edit seed; for(const auto& [id,row]:world) for(const auto& [a,v]:row) seed.add.push_back({id,a,v});
    db.send(client::edit_request(seed));
    std::vector<query> queries; std::vector<uint64_t> handles;
    const size_t count=workload=="shared32"||workload=="distinct32"?32:1;
    for(size_t i=0;i<count;++i) queries.push_back(make_query(shape,workload=="distinct32"?int(i):0));
    if(!subscription) for(const auto& q:queries) handles.push_back(db.subscribe(q));
    const auto subscribe_request=client::subscribe_request(queries.front());
    std::vector<double> times; uint64_t notification_digest=0, final_digest=0, final_facts=0;
    for(size_t step=0;step<samples+warmup;++step) {
      const bool verify=step==0||step+1==warmup||step+1==warmup+samples;
      edit delta; uint64_t id=1+random()%n;
      if(!subscription) {
        if(workload=="predicate_flip") delta.add.push_back({id,"group",world.at(id).at("group")=="0"?"1":"0"});
        else if(workload=="remote") delta.add.push_back({owner_base+1,"region",step%2?"common":"rare"});
        else if(workload=="delete_reinsert") {
          id=n+1;
          if(step%2==0) for(const auto& [a,v]:task(id)) delta.add.push_back({id,a,v});
          else for(const auto& [a,v]:world.at(id)) delta.remove.push_back({id,a,v});
        } else if(workload=="unrelated") delta.add.push_back({id,"unrelated",std::to_string(step)});
        else if(workload=="noop") delta.add.push_back({id,"title",world.at(id).at("title")});
        else {
          for(size_t j=0;j<(workload=="batch32"?32:1);++j) {
            uint64_t e=1+(id-1+j)%n;
            delta.add.push_back({e,"title","edited "+std::to_string(step)});
          }
        }
      }
      std::vector<facts> before;
      if(verify&&!subscription) for(size_t i=0;i<count;++i) before.push_back(expected(world,shape,workload=="distinct32"?int(i):0));
      const auto request=subscription?subscribe_request:client::edit_request(delta);
      const auto start=std::chrono::steady_clock::now();
      auto reply=db.send(request);
      const double elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
      if(step>=warmup) times.push_back(elapsed);
      reader r{reply}; std::vector<uint64_t> notified;
      uint64_t temporary=0;
      if(subscription) temporary=r.u64();
      else { auto k=r.u32(); for(uint32_t j=0;j<k;++j) notified.push_back(r.u64()); }
      r.end(); notification_digest=notification_digest*31+notified.size();
      apply_edit(world,delta);
      if(verify) {
        std::vector<uint64_t> expected_notifications;
        for(size_t i=0;i<count;++i) {
          auto wanted=expected(world,shape,workload=="distinct32"?int(i):0);
          require(db.read(subscription?temporary:handles[i])==wanted,"query contents differ from independent oracle");
          if(!subscription && before[i]!=wanted) expected_notifications.push_back(handles[i]);
          final_digest^=digest(wanted)+i; final_facts=wanted.size();
        }
        require(expected_notifications==notified,"incorrect query notification IDs");
      }
      if(subscription) db.unsubscribe(temporary);
    }
    const auto mean=std::accumulate(times.begin(),times.end(),0.0)/samples;
    std::sort(times.begin(),times.end());
    std::cout<<"workload,entities,samples,p50,p95,p99,max,mean,final_facts,result_digest,notification_digest\n";
    std::cout<<workload<<','<<n<<','<<samples<<','<<times[samples/2]<<','<<times[samples*95/100]<<','
      <<times[samples*99/100]<<','<<times.back()<<','<<mean<<','<<final_facts<<','<<final_digest<<','<<notification_digest<<'\n';
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
