#pragma once
#include "datafriend/engine.h"
#include "profiling/metrics.h"
#include <utility>
#include <bit>

namespace ui {

// Small native equivalent of the Flutter query builder: subscribe once, build
// initially, then rebuild only when modify() returns this subscription handle.
// All callbacks run synchronously after publication; callbacks only read views.
class view_bindings {
  struct binding {
    datafriend::query_result result;
    std::function<void(const datafriend::query_result&)> build;
  };
  datafriend::engine& db_;
  std::map<uint64_t,binding> bindings_;
 public:
  explicit view_bindings(datafriend::engine& db):db_(db) {}
  view_bindings(const view_bindings&)=delete;
  view_bindings& operator=(const view_bindings&)=delete;
  ~view_bindings() { for(const auto& [id,_]:bindings_)db_.unsubscribe(id); }
  uint64_t watch(datafriend::query q,
                 std::function<void(const datafriend::query_result&)> build) {
    task_profile::timer timing(task_profile::current.subscribe);
    if(task_profile::enabled)++task_profile::current.subscriptions;
    auto [id,result]=db_.subscribe(std::move(q));
    auto [it,_]=bindings_.emplace(id,binding{std::move(result),std::move(build)});
    try { it->second.build(it->second.result); }
    catch(...) { unwatch(id);throw; }
    return id;
  }
  void unwatch(uint64_t id) {
    task_profile::timer timing(task_profile::current.unsubscribe);
    if(bindings_.erase(id)){db_.unsubscribe(id);if(task_profile::enabled)++task_profile::current.unsubscriptions;}
  }
  size_t size() const { return bindings_.size(); }
  void publish(const std::vector<uint64_t>& changed) {
    for(auto id:changed)if(auto it=bindings_.find(id);it!=bindings_.end())
    {
      // A list builder can remove row subscriptions during publication. Keep the
      // current callback alive, and skip handles that have since been removed.
      auto callback=it->second.build;auto result=it->second.result;
      callback(result);
    }
  }
};

using attribute=datafriend::attribute;
struct var { int32_t id; };
struct entity { uint32_t id; };
inline std::string name(attribute a) {
  for(auto& d:datafriend::attribute_definitions)if(d.id==uint32_t(a))return std::string(d.name);
  throw std::invalid_argument("Unknown attribute");
}
struct value {
  datafriend::bytes bytes;
  value(bool v):bytes{char(1),char(v)} {}
  value(int v):value(int64_t(v)) {}
  value(uint32_t v):value(int64_t(v)) {}
  value(int64_t v):bytes(9,'\0') {
    bytes[0]=3;auto bits=uint64_t(v);for(int i=0;i<8;i++)bytes[i+1]=char(bits>>(56-i*8));
  }
  value(entity e):bytes(datafriend::entity_value(e.id)) {}
  value(const char* s):value(std::string(s)) {}
  value(std::string s):bytes(5,'\0') {
    if(s.size()>UINT32_MAX)throw std::length_error("Text too long");
    bytes[0]=2;for(int i=0;i<4;i++)bytes[i+1]=char(uint32_t(s.size())>>(24-i*8));bytes+=s;
  }
};
struct entity_term {
  datafriend::term term;
  entity_term(var v):term{v.id,{}} {}
  entity_term(ui::entity e):entity_term(e.id) {}
  entity_term(uint32_t id):term{-1,datafriend::entity_value(id)} {}
};
struct value_term {
  datafriend::term term;
  value_term(var v):term{v.id,{}} {}
  value_term(value v):term{-1,std::move(v.bytes)} {}
  value_term(int v):value_term(value(v)) {}
  value_term(int64_t v):value_term(value(v)) {}
  value_term(uint32_t v):value_term(value(v)) {}
  value_term(entity v):value_term(value(v)) {}
  value_term(bool v):value_term(value(v)) {}
  value_term(const char* v):value_term(value(v)) {}
  value_term(std::string v):value_term(value(std::move(v))) {}
};
struct clause { entity_term entity;attribute field;value_term value; };
inline datafriend::query select(std::initializer_list<std::pair<clause,bool>> clauses) {
  datafriend::query q;
  for(auto& [c,include]:clauses) {
    if(include)q.projection.push_back(uint32_t(q.clauses.size()));
    q.clauses.push_back({c.entity.term,name(c.field),c.value.term});
  }
  return q;
}
struct fact { uint32_t entity;attribute field;value data; };
class rows {
  datafriend::query_result result_;
  const datafriend::bytes& read(uint32_t id,attribute a) const {
    auto* b=result_.value(id,uint32_t(a));if(!b)throw std::runtime_error("Missing projected field");return *b;
  }
 public:
  explicit rows(datafriend::query_result r):result_(std::move(r)) {}
  bool empty() const { return result_.empty(); }
  bool contains(uint32_t id,attribute a) const { return result_.value(id,uint32_t(a)); }
  std::vector<uint32_t> entities(attribute order=attribute(0)) const { return result_.entities(uint32_t(order)); }
  std::string text(uint32_t id,attribute a) const {
    auto& b=read(id,a);if(b.size()<5||b[0]!=2)throw std::runtime_error("Expected text");
    uint32_t n=0;for(int i=1;i<5;i++)n=(n<<8)|uint8_t(b[i]);
    if(n!=b.size()-5)throw std::runtime_error("Invalid text");return b.substr(5);
  }
  bool flag(uint32_t id,attribute a) const {
    auto& b=read(id,a);if(b.size()!=2||b[0]!=1)throw std::runtime_error("Expected boolean");return b[1]!=0;
  }
  int64_t integer(uint32_t id,attribute a) const {
    auto& b=read(id,a);if(b.size()!=9||(b[0]!=3&&b[0]!=6))throw std::runtime_error("Expected integer/reference");
    uint64_t n=0;for(int i=1;i<9;i++)n=(n<<8)|uint8_t(b[i]);return std::bit_cast<int64_t>(n);
  }
  uint64_t count(attribute a,value v) const { return result_.count_value(uint32_t(a),v.bytes); }
};
// Ordinary C++ ownership controls subscription lifetime. No widget lifecycle,
// build-context object, virtual tree or state scheduler is needed.
class subscription {
  view_bindings* owner_=nullptr;uint64_t id_=0;
 public:
  subscription()=default;
  subscription(view_bindings& owner,uint64_t id):owner_(&owner),id_(id) {}
  subscription(const subscription&)=delete;
  subscription& operator=(const subscription&)=delete;
  subscription(subscription&& other) noexcept:owner_(std::exchange(other.owner_,nullptr)),id_(other.id_) {}
  subscription& operator=(subscription&& other) noexcept {
    if(this!=&other){reset();owner_=std::exchange(other.owner_,nullptr);id_=other.id_;}return *this;
  }
  ~subscription(){reset();}
  void reset(){if(owner_){owner_->unwatch(id_);owner_=nullptr;}}
};
class store {
 public:
  datafriend::engine engine;
  view_bindings bindings;
  explicit store(const std::string& path):engine(path),bindings(engine) {}
  subscription watch(datafriend::query q,std::function<void(const rows&)> build) {
    auto id=bindings.watch(std::move(q),[build=std::move(build)](auto& r){build(rows(r));});
    return {bindings,id};
  }
  void edit(std::initializer_list<fact> facts){edit(std::vector<fact>(facts));}
  void edit(const std::vector<fact>& facts) {
    datafriend::edit e;
    {task_profile::timer t(task_profile::current.encode);
      for(auto& f:facts)e.add.push_back({f.entity,name(f.field),f.data.bytes});}
    std::vector<uint64_t> changed;
    {task_profile::timer t(task_profile::current.engine);changed=engine.modify(std::move(e));}
    {task_profile::timer t(task_profile::current.publish);bindings.publish(changed);}
    if(task_profile::enabled)++task_profile::current.edits;
  }
  void remove(uint32_t id) {
    std::vector<uint64_t> changed;
    {task_profile::timer t(task_profile::current.engine);changed=engine.import_entities({{id,std::nullopt}});}
    {task_profile::timer t(task_profile::current.publish);bindings.publish(changed);}
    if(task_profile::enabled)++task_profile::current.edits;
  }
  uint32_t new_entity() { return uint32_t(engine.new_entity()); }
};
template<class Action> bool attempt(std::string& error,Action&& action) {
  try { action();error.clear();return true; }
  catch(const std::exception& e){error=e.what();return false;}
}
} // namespace ui
