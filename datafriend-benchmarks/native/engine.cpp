#include "datafriend/engine.h"
#include "datafriend/codec.h"
#include "lmdb.h"
#include "async_flush.h"
#include "sync_snapshot.h"

#include "ankerl/unordered_dense.h"
#define XXH_INLINE_ALL
#include "../../third_party/xxhash/xxhash.h"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <future>
#include <mutex>
#include <thread>
#include <set>
#include <array>
#include <bit>
#include <tuple>
#include <algorithm>
#include <limits>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <cstdio>
#include <atomic>

#ifdef DATAFRIEND_PROFILE
#include <chrono>
namespace {
using profile_clock = std::chrono::steady_clock;
thread_local datafriend::edit_profile* active_profile = nullptr;
struct profile_session {
  explicit profile_session(datafriend::edit_profile& p) { p={}; active_profile=&p; }
  ~profile_session() { active_profile=nullptr; }
};
struct profile_timer {
  uint64_t datafriend::edit_profile::*field;
  profile_clock::time_point start;
  bool running;
  explicit profile_timer(uint64_t datafriend::edit_profile::*f)
    :field(f),start(active_profile?profile_clock::now():profile_clock::time_point{}),running(active_profile!=nullptr) {}
  void stop() { if(running) { active_profile->*field += std::chrono::duration_cast<std::chrono::nanoseconds>(profile_clock::now()-start).count(); running=false; } }
  ~profile_timer() { stop(); }
};
#if defined(DATAFRIEND_PROFILE_INDEXES) && defined(__ANDROID__) && defined(__aarch64__)
// Fine-grained diagnostics on Android arm64: avoid a clock_gettime call around
// every tiny map operation. Use the same architected counter as the arm64 vDSO,
// with an ISB/compiler barrier at each boundary. Coarse clocks stay unchanged.
struct index_profile_timer {
  uint64_t datafriend::edit_profile::*field;
  datafriend::edit_profile* target;
  uint64_t start=0, hz=0;
  static uint64_t ticks() {
    uint64_t value;
    asm volatile("isb; mrs %0, cntvct_el0" : "=r"(value) : : "memory");
    return value;
  }
  static uint64_t frequency() {
    static const uint64_t value=[] {
      uint64_t frequency; asm volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
      if(!frequency) throw std::runtime_error("architected timer frequency unavailable");
      return frequency;
    }();
    return value;
  }
  explicit index_profile_timer(uint64_t datafriend::edit_profile::*f):field(f),target(active_profile) {
    if(target) { hz=frequency(); start=ticks(); }
  }
  void stop() {
    if(target) {
      const auto delta=ticks()-start;
      target->*field+=(delta/hz)*1000000000ULL+(delta%hz)*1000000000ULL/hz;
      target=nullptr;
    }
  }
  ~index_profile_timer() { stop(); }
};
#else
using index_profile_timer=profile_timer;
#endif
}
#define DF_START(field) profile_timer timer_##field(&edit_profile::field)
#define DF_STOP(field) timer_##field.stop()
#define DF_COUNT(field) do { if(active_profile) ++active_profile->field; } while(false)
#else
#define DF_START(field)
#define DF_STOP(field)
#define DF_COUNT(field)
#endif
#ifdef DATAFRIEND_PROFILE_INDEXES
#define DF_INDEX_START(field) index_profile_timer timer_##field(&edit_profile::field)
#define DF_INDEX_STOP(field) timer_##field.stop()
#else
#define DF_INDEX_START(field)
#define DF_INDEX_STOP(field)
#endif

namespace datafriend {
namespace dense = ankerl::unordered_dense;
namespace {
uint64_t hash(std::string_view b) { return XXH64(b.data(),b.size(),0); }
bytes id_bytes(uint64_t id) { writer w; w.u64(id); return w.data; }
void valid_entity(uint64_t id) {
  if(id>UINT32_MAX) throw std::invalid_argument("entity ID exceeds namespace uint32 range");
}
void valid(const triple& t) {
  valid_entity(t.entity);
  if(t.attribute.empty() || t.attribute.size()>1024 || t.value.size()>65527)
    throw std::length_error("invalid attribute or oversized value");
}
// Exact structural identity, including clause/projection order and variable IDs.
// Length prefixes avoid ambiguous concatenation; equality uses the complete bytes.
bytes query_key(const query& q) {
  writer w; w.u8(q.all); w.u32(uint32_t(q.clauses.size()));
  auto term_key=[&](const term& t) { w.u32(uint32_t(t.variable)); w.blob(t.constant); };
  for(const auto& c:q.clauses) { term_key(c.entity); w.blob(c.attribute); term_key(c.value); }
  if(q.projection.size()>UINT32_MAX) throw std::length_error("projection too large");
  w.u32(uint32_t(q.projection.size())); for(auto i:q.projection) w.u32(i);
  return w.data;
}
void check(int rc) { if(rc) throw std::runtime_error(mdb_strerror(rc)); }
MDB_val val(std::string_view s) { return {s.size(),const_cast<char*>(s.data())}; }
struct transaction {
  MDB_txn* p=nullptr;
  transaction(MDB_env* env, unsigned flags=0) { check(mdb_txn_begin(env,nullptr,flags,&p)); }
  ~transaction() { if(p) mdb_txn_abort(p); }
  void commit() { auto* t=std::exchange(p,nullptr); check(mdb_txn_commit(t)); }
};
}
bytes entity_value(uint64_t e) { valid_entity(e); writer w; w.u8(6); w.u64(e); return w.data; }
boxed_value::boxed_value(bytes b):value(std::move(b)),hash(datafriend::hash(value)) {}
uint64_t pair_key(uint32_t e,uint32_t a) noexcept { return (uint64_t(e)<<32)|a; }
uint64_t triple_hash(uint64_t key,const boxed_value& value) {
  writer w; w.u64(key); w.u64(value.hash); return hash(w.data);
}
std::optional<uint32_t> view_scope::find(std::string_view name) const {
  auto it=ids_.find(name);
  return it==ids_.end()?std::nullopt:std::optional<uint32_t>(it->second);
}
std::string view_scope::name_of(uint32_t id) const { return names_.at(id); }
uint64_t view_scope::next_id() const { return next_; }
void view_scope::define(uint32_t id,std::string name) {
  if(name.empty()||name.size()>1024) throw std::invalid_argument("invalid attribute name");
  if(auto it=ids_.find(name); it!=ids_.end()) {
    if(it->second!=id) throw std::runtime_error("inconsistent attribute dictionary");
    return;
  }
  auto [entry,inserted]=names_.emplace(id,name);
  if(!inserted) throw std::runtime_error("duplicate attribute ID");
  try { ids_.emplace(std::move(name),id); } catch(...) { names_.erase(entry); throw; }
  next_=std::max(next_,uint64_t(id)+1);
}
uint32_t view_scope::intern(std::string name) {
  if(auto id=find(name)) return *id;
  throw std::invalid_argument("unknown shared attribute: "+name);
}
bytes encode_entity(uint64_t e,const entity_record& r) {
  valid_entity(e);
  writer w; w.u8(1); w.u32(uint32_t(r.size()));
  for(const auto& [a,v]:r) { valid({e,a,v}); w.blob(a); w.blob(v); }
  writer identity; identity.u64(e); identity.data+=w.data;
  writer out; out.u64(hash(identity.data)); out.data+=w.data; return out.data;
}
entity_record decode_entity(uint64_t e,std::string_view input) {
  valid_entity(e);
  if(input.size()>64*1024*1024) throw std::length_error("oversized entity export");
  reader r{input}; auto expected=r.u64(); auto payload=r.data;
  writer identity; identity.u64(e); identity.data.append(payload);
  if(hash(identity.data)!=expected) throw std::invalid_argument("entity fingerprint mismatch");
  if(r.u8()!=1) throw std::invalid_argument("unsupported entity codec");
  entity_record out; std::string previous;
  auto n=r.count(8);
  for(uint32_t i=0;i<n;++i) {
    auto a=r.blob(),v=r.blob(); valid({e,a,v});
    if(i && a<=previous) throw std::invalid_argument("noncanonical attributes");
    previous=a; out.emplace(std::move(a),std::move(v));
  }
  r.end(); return out;
}
bytes encode_fact(uint64_t key,std::string_view value) {
  if(value.size()>65527) throw std::length_error("oversized value");
  auto kb=id_bytes(key);
  XXH64_state_t state; XXH64_reset(&state,0);
  XXH64_update(&state,kb.data(),kb.size()); XXH64_update(&state,value.data(),value.size());
  writer out; out.u64(XXH64_digest(&state)); out.data.append(value); return out.data;
}
bytes decode_fact(uint64_t key,std::string_view record) {
  if(record.size()<8||record.size()>65535) throw std::length_error("invalid fact record size");
  reader r{record}; auto fingerprint=r.u64();
  auto kb=id_bytes(key);
  XXH64_state_t state; XXH64_reset(&state,0);
  XXH64_update(&state,kb.data(),kb.size()); XXH64_update(&state,r.data.data(),r.data.size());
  if(XXH64_digest(&state)!=fingerprint) throw std::invalid_argument("fact fingerprint mismatch");
  return bytes(r.data);
}
std::vector<uint32_t> query_result::entities(uint32_t order,uint32_t secondary) const {
  check();
  // Missing sorts after every integer, including INT64_MAX.
  using sort_value=std::pair<bool,int64_t>;
  std::vector<std::tuple<sort_value,sort_value,uint32_t>> keys;
  dense::set<uint32_t> seen;
  for(const auto& [key,box]:data_->facts) {
    const auto entity=uint32_t(key>>32);
    if(!seen.insert(entity).second) continue;
    auto number=[&](uint32_t attr)->sort_value {
      if(!attr) return {false,0};
      auto it=data_->facts.find(pair_key(entity,attr));
      if(it==data_->facts.end()) return {true,0};
      reader r{it->second->value};
      if(r.u8()!=3) throw std::invalid_argument("order attribute must be an int64");
      auto n=std::bit_cast<int64_t>(r.u64()); r.end(); return {false,n};
    };
    keys.emplace_back(number(order),number(secondary),entity);
  }
  std::sort(keys.begin(),keys.end());
  std::vector<uint32_t> ids; ids.reserve(keys.size());
  for(auto [primary,secondary,entity]:keys) ids.push_back(entity);
  return ids;
}
bytes query_result::encode() const {
  check(); writer w; w.u32(uint32_t(data_->facts.size()));
  for(const auto& [key,v]:data_->facts) {
    w.u64(key>>32); w.blob(data_->scope->name_of(uint32_t(key))); w.blob(v->value);
  }
  return w.data;
}

struct engine::impl {
#ifdef DATAFRIEND_PROFILE
  edit_profile profile;
#endif
  std::shared_ptr<view_scope> scope;
  using entities = dense::map<uint32_t,value_ref>;
  dense::map<uint32_t,entities> aev;
  dense::map<uint32_t,dense::map<value_ref,dense::set<uint32_t>,value_hash,value_equal>> ave;
  struct compiled_clause {
    term entity; uint32_t attribute; term value; value_ref constant;
    bool bind_value=false, projected=false;
    std::optional<uint64_t> fixed_dependency;
  };
  struct entity_group { term entity; std::vector<uint32_t> checks; };
  struct compiled_query {
    std::vector<compiled_clause> clauses;
    std::vector<uint32_t> projection;
    bool all=false, entity_local=false;
    std::vector<entity_group> entities;
    // One necessary literal predicate partitions entity-local subscribers.
    // All other conditions are still checked by the normal query evaluator.
    std::optional<uint32_t> route_predicate;
  };
  struct query_group {
    bytes key; compiled_query q; query_result result; size_t owners=0;
#ifdef DATAFRIEND_DIRECT_SUBSCRIPTIONS
    // Reverse ownership for cleanup; candidates satisfy the selected predicate,
    // even if other required attributes are absent or other conditions fail.
    dense::set<uint32_t> routed_entities{};
#endif
  };
  // std::map keeps group addresses stable. View handles and dependency indexes
  // borrow those addresses until the last handle unsubscribes.
  std::map<bytes,query_group> groups;
  std::map<uint64_t,query_group*> views;
  dense::map<uint32_t,dense::set<query_group*>> subscribers;
  dense::map<uint64_t,dense::set<query_group*>> fixed_subscribers;
  // Borrow pending edit bytes for routing without allocating another value box.
  struct routing_value { std::string_view value; uint64_t hash; };
  struct routing_hash {
    using is_transparent=void;
    size_t operator()(const value_ref& value) const { return value->hash; }
    size_t operator()(const routing_value& value) const { return value.hash; }
  };
  struct routing_equal {
    using is_transparent=void;
    bool operator()(const value_ref& a,const value_ref& b) const { return value_equal{}(a,b); }
    bool operator()(const routing_value& a,const value_ref& b) const { return a.value==b->value; }
    bool operator()(const value_ref& a,const routing_value& b) const { return a->value==b.value; }
  };
  using value_subscribers=dense::map<value_ref,dense::set<query_group*>,routing_hash,routing_equal>;
#ifdef DATAFRIEND_DIRECT_SUBSCRIPTIONS
  dense::map<uint64_t,dense::set<query_group*>> direct_subscribers;
  dense::map<uint32_t,value_subscribers> match_subscribers;
  void link_candidate(query_group* group,uint32_t entity,bool present) {
    if(present) {
      if(!group->routed_entities.insert(entity).second) return;
      for(const auto& c:group->q.clauses)
        direct_subscribers[pair_key(entity,c.attribute)].insert(group);
    } else {
      if(!group->routed_entities.erase(entity)) return;
      for(const auto& c:group->q.clauses) {
        auto it=direct_subscribers.find(pair_key(entity,c.attribute));
        if(it==direct_subscribers.end()) continue;
        it->second.erase(group);
        if(it->second.empty()) direct_subscribers.erase(it);
      }
    }
  }
  void seed_candidates(query_group* group) {
    const auto& predicate=group->q.clauses[*group->q.route_predicate];
    auto a=ave.find(predicate.attribute);
    if(a==ave.end()) return;
    auto v=a->second.find(predicate.constant);
    if(v!=a->second.end()) for(auto entity:v->second) link_candidate(group,entity,true);
  }
  void rebuild_candidates() {
    direct_subscribers.clear();
    for(auto& [key,group]:groups) {
      group.routed_entities.clear();
      if(group.q.route_predicate) seed_candidates(&group);
    }
  }
#else
  // changed attribute -> required predicate attribute -> value -> groups
  dense::map<uint32_t,dense::map<uint32_t,value_subscribers>> predicate_subscribers;
#endif
  dense::set<query_group*> wildcard;
  void erase_group(query_group* group) {
    auto position=groups.find(group->key);
    wildcard.erase(group);
#ifdef DATAFRIEND_DIRECT_SUBSCRIPTIONS
    if(group->q.route_predicate) {
      while(!group->routed_entities.empty()) link_candidate(group,*group->routed_entities.begin(),false);
      const auto& predicate=group->q.clauses[*group->q.route_predicate];
      auto a=match_subscribers.find(predicate.attribute);
      if(a!=match_subscribers.end()) {
        auto v=a->second.find(predicate.constant);
        if(v!=a->second.end()) {
          v->second.erase(group);
          if(v->second.empty()) a->second.erase(v);
        }
        if(a->second.empty()) match_subscribers.erase(a);
      }
      groups.erase(position);
      return;
    }
#endif
    for(const auto& c:group->q.clauses) {
#ifndef DATAFRIEND_DIRECT_SUBSCRIPTIONS
      if(group->q.route_predicate) {
        const auto& predicate=group->q.clauses[*group->q.route_predicate];
        auto attributes=predicate_subscribers.find(c.attribute);
        if(attributes==predicate_subscribers.end()) continue;
        auto values=attributes->second.find(predicate.attribute);
        if(values==attributes->second.end()) continue;
        auto bucket=values->second.find(predicate.constant);
        if(bucket!=values->second.end()) {
          bucket->second.erase(group);
          if(bucket->second.empty()) values->second.erase(bucket);
        }
        if(values->second.empty()) attributes->second.erase(values);
        if(attributes->second.empty()) predicate_subscribers.erase(attributes);
        continue;
      }
#endif
      if(c.fixed_dependency) {
        auto it=fixed_subscribers.find(*c.fixed_dependency);
        if(it!=fixed_subscribers.end()) {
          it->second.erase(group);
          if(it->second.empty()) fixed_subscribers.erase(it);
        }
        continue;
      }
      auto it=subscribers.find(c.attribute);
      if(it!=subscribers.end()) {
        it->second.erase(group);
        if(it->second.empty()) subscribers.erase(it);
      }
    }
    groups.erase(position);
  }
  uint64_t next_view=1, version=0, recomputed=0;
#ifdef DATAFRIEND_TESTING
  bool fail_commit=false;
  std::atomic<bool> fail_flush=false;
#endif
  MDB_env* env=nullptr;
  std::unique_ptr<sync_snapshot> network;
  std::unique_ptr<async_flush> flusher;
  durability mode;
  MDB_dbi db=0, meta=0;
  uint64_t next_entity=1;
  int lock=-1;
  std::shared_ptr<query_result::access> gate=std::make_shared<query_result::access>();
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::function<void()>> jobs;
  bool stop=false;
  std::thread owner;
  impl(const std::string& path,durability persistence,size_t map_bytes,std::string name):scope(std::make_shared<view_scope>(std::move(name))),mode(persistence) {
    if(scope->name.empty()||scope->name.size()>1024) throw std::invalid_argument("invalid namespace");
    // Initialization itself runs on the owner too; constructor propagates errors.
    owner=std::thread([this]{
      for(;;) {
        std::function<void()> job;
        { std::unique_lock guard(mutex); wake.wait(guard,[&]{return stop||!jobs.empty();});
          if(stop && jobs.empty()) break;
          job=std::move(jobs.front()); jobs.pop_front(); }
        job();
      }
    });
    try { call([&]{ open(path,map_bytes); }); }
    catch(...) { shutdown(); throw; }
  }
  template<class F> auto call(F&& f)->std::invoke_result_t<F> {
    using R=std::invoke_result_t<F>;
    auto task=std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
    auto result=task->get_future();
    { std::lock_guard guard(mutex); jobs.emplace_back([task]{(*task)();}); }
    wake.notify_one(); return result.get();
  }
  void shutdown() noexcept {
    if(owner.joinable()) call([&]{network.reset();});
    { std::lock_guard guard(mutex); stop=true; } wake.notify_one();
    if(owner.joinable()) owner.join();
    if(flusher) {
      if(int rc=flusher->stop()) std::fprintf(stderr,"Datafriend final flush failed: %s\n",mdb_strerror(rc));
      flusher.reset();
    }
    if(env) { mdb_env_close(env); env=nullptr; }
    if(lock>=0) { flock(lock,LOCK_UN); ::close(lock); lock=-1; }
  }
  ~impl() { shutdown(); }
  void open(const std::string& path,size_t map_bytes) {
    if(mode!=durability::buffered && mode!=durability::synchronous && mode!=durability::asynchronous)
      throw std::invalid_argument("invalid durability mode");
    std::filesystem::create_directories(path);
    lock=::open((path+"/datafriend.owner").c_str(),O_CREAT|O_RDWR,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)) throw std::runtime_error("Datafriend directory already owned or inaccessible");
    check(mdb_env_create(&env)); check(mdb_env_set_mapsize(env,map_bytes)); check(mdb_env_set_maxdbs(env,2));
    // Reader slots belong to transaction objects: a network snapshot may coexist
    // with the owner's short-lived query/edit read transactions.
    check(mdb_env_open(env,path.c_str(),MDB_NOTLS|(mode==durability::synchronous?0:MDB_NOSYNC),0600));
    if(mode==durability::asynchronous) {
      mdb_filehandle_t fd; check(mdb_env_get_fd(env,&fd));
      flusher=std::make_unique<async_flush>([
#ifdef DATAFRIEND_TESTING
        this,
#endif
        fd] {
#ifdef DATAFRIEND_TESTING
        if(fail_flush.load()) return EIO;
#endif
        // Equivalent to AELMDB's non-WRITEMAP sync, without racing its mutable
        // meta pages in mdb_env_sync(). The environment outlives this worker.
        int rc;
        do {
#ifdef __APPLE__
          rc=fcntl(fd,F_FULLFSYNC);
#else
          rc=fsync(fd);
#endif
        } while(rc<0 && errno==EINTR);
        return rc<0?errno:0;
      });
    }
    transaction t(env);
    check(mdb_dbi_open(t.p,"meta",MDB_CREATE,&meta));
    const auto codec=std::string("datafriend/3;xor64;ea32;fact-v1;")+std::string(attribute_schema);
    auto k=val("codec"),v=val(codec); MDB_val existing;
    int rc=mdb_get(t.p,meta,&k,&existing);
    if(rc==MDB_NOTFOUND) check(mdb_put(t.p,meta,&k,&v,0));
    else { check(rc); if(std::string_view(static_cast<char*>(existing.mv_data),existing.mv_size)!=std::string_view(static_cast<char*>(v.mv_data),v.mv_size)) throw std::runtime_error("incompatible Datafriend codec"); }
    check(mdb_dbi_open(t.p,"facts",MDB_CREATE|MDB_AGG_ENTRIES|MDB_AGG_HASHSUM,&db));
    if(rc==MDB_NOTFOUND) check(mdb_set_hash_offset(t.p,db,0));
    else { int offset; check(mdb_get_hash_offset(t.p,db,&offset)); if(offset!=0) throw std::runtime_error("incompatible hash offset"); }
    auto nk=val("namespace"); MDB_val nv;
    int nr=mdb_get(t.p,meta,&nk,&nv);
    if(nr==MDB_NOTFOUND) { auto name=val(scope->name); check(mdb_put(t.p,meta,&nk,&name,0)); }
    else { check(nr); if(std::string_view(static_cast<char*>(nv.mv_data),nv.mv_size)!=scope->name) throw std::runtime_error("store belongs to another namespace"); }
    auto counter=val("next_entity"); MDB_val cv;
    int cr=mdb_get(t.p,meta,&counter,&cv);
    if(cr!=MDB_NOTFOUND) { check(cr); reader r{{static_cast<char*>(cv.mv_data),cv.mv_size}}; next_entity=r.u64(); r.end(); }
    if(next_entity<1||next_entity>uint64_t(UINT32_MAX)+1) throw std::runtime_error("invalid entity counter");
    t.commit(); written();
    rebuild();
  }
  void healthy() { if(gate->poisoned) throw std::runtime_error("Datafriend index recovery failed; reopen store"); }
  void writable() { healthy(); if(flusher) flusher->check(); }
  void written() { if(flusher) flusher->written(); }
  uint32_t intern_attribute(const std::string& name) {
    healthy(); return scope->intern(name);
  }
  void write_counter(MDB_txn* tx,uint64_t next) {
    auto key=val("next_entity"); auto encoded=id_bytes(next); auto value=val(encoded);
    check(mdb_put(tx,meta,&key,&value,0));
  }
  compiled_query compile(const query& q) {
    compiled_query out{{},q.projection,q.all,false,{}, {}};
    for(const auto& c:q.clauses) {
      const auto attribute=intern_attribute(c.attribute);
      std::optional<uint64_t> dependency;
      if(c.entity.variable<0) {
        reader r{std::string_view(c.entity.constant).substr(1)};
        dependency=pair_key(uint32_t(r.u64()),attribute);
      }
      out.clauses.push_back({c.entity,attribute,c.value,
        c.value.variable<0?std::make_shared<const boxed_value>(c.value.constant):value_ref{},
        false,false,dependency});
    }
    std::array<uint32_t,64> uses{};
    for(const auto& c:q.clauses) {
      if(c.entity.variable>=0) ++uses[c.entity.variable];
      if(c.value.variable>=0) ++uses[c.value.variable];
    }
    for(auto i:q.projection) out.clauses[i].projected=true;
    for(uint32_t i=0;i<out.clauses.size();++i) {
      auto& c=out.clauses[i];
      // Results project matched facts, not binding tuples. A value occurring
      // only here needs no binding slot, even when its fact is projected.
      c.bind_value=c.value.variable>=0 && uses[c.value.variable]>1;
      auto group=std::find_if(out.entities.begin(),out.entities.end(),[&](const entity_group& g) {
        return g.entity.variable==c.entity.variable &&
          (g.entity.variable>=0 || g.entity.constant==c.entity.constant);
      });
      if(group==out.entities.end()) out.entities.push_back({c.entity,{i}});
      else group->checks.push_back(i);
    }
    for(auto& g:out.entities) std::stable_sort(g.checks.begin(),g.checks.end(),[&](uint32_t a,uint32_t b) {
      auto priority=[&](uint32_t i) {
        const auto& c=out.clauses[i];
        return c.value.variable<0?0:c.bind_value?1:!c.projected?2:3;
      };
      return priority(a)<priority(b);
    });
    out.entity_local=out.entities.size()==1;
    if(out.entity_local && out.entities.front().entity.variable>=0) {
      for(uint32_t i=0;i<out.clauses.size();++i) if(out.clauses[i].constant) {
        out.route_predicate=i; break;
      }
    }
    return out;
  }
  const value_ref* current(uint64_t key) const {
    auto a=aev.find(uint32_t(key)); if(a==aev.end()) return nullptr;
    auto e=a->second.find(uint32_t(key>>32)); return e==a->second.end()?nullptr:&e->second;
  }
  void index(uint64_t key,const std::optional<bytes>& next) {
    const auto a=uint32_t(key), id=uint32_t(key>>32);
    DF_INDEX_START(index_lookup_ns);
    auto ai=aev.find(a);
    auto ei=ai==aev.end()?entities::iterator{}:ai->second.find(id);
    DF_INDEX_STOP(index_lookup_ns);
    if(ai!=aev.end() && ei!=ai->second.end()) {
        const auto old=ei->second;
        {
        DF_INDEX_START(index_ave_remove_ns);
        auto av=ave.find(a);
        if(av!=ave.end()) {
          auto vi=av->second.find(old);
          if(vi!=av->second.end()) { vi->second.erase(id); if(vi->second.empty()) av->second.erase(vi); }
          if(av->second.empty()) ave.erase(av);
        }
        }
        {
        DF_INDEX_START(index_aev_remove_ns);
        ai->second.erase(ei);
        if(ai->second.empty()) aev.erase(ai);
        }
    }
    if(next) {
      DF_INDEX_START(index_box_ns);
      auto value=std::make_shared<const boxed_value>(*next);
      DF_INDEX_STOP(index_box_ns);
      { DF_INDEX_START(index_aev_insert_ns); aev[a][id]=value; }
      { DF_INDEX_START(index_ave_insert_ns); ave[a][value].insert(id); }
    }
  }
  void rebuild() {
    aev.clear(); ave.clear(); transaction t(env,MDB_RDONLY);
    MDB_cursor* raw=nullptr; check(mdb_cursor_open(t.p,db,&raw));
    std::unique_ptr<MDB_cursor,decltype(&mdb_cursor_close)> c(raw,mdb_cursor_close);
    MDB_val k,v; int rc;
    while((rc=mdb_cursor_get(c.get(),&k,&v,MDB_NEXT))==0) {
      if(k.mv_size!=8) throw std::runtime_error("invalid fact key");
      reader r{std::string_view(static_cast<char*>(k.mv_data),k.mv_size)}; auto key=r.u64();
      scope->name_of(uint32_t(key)); // Reject facts outside the shared schema.
      if((key>>32)>=next_entity) throw std::runtime_error("entity counter behind stored IDs");
      index(key,decode_fact(key,{static_cast<char*>(v.mv_data),v.mv_size}));
    }
    if(rc!=MDB_NOTFOUND) check(rc);
  }
  // Only whole-entity convenience operations need a prefix scan. Ordinary
  // triple edits use the existing indexes and never reconstruct an entity.
  entity_record read(MDB_txn* t,uint64_t id) {
    valid_entity(id);
    MDB_cursor* raw; check(mdb_cursor_open(t,db,&raw));
    std::unique_ptr<MDB_cursor,decltype(&mdb_cursor_close)> c(raw,mdb_cursor_close);
    auto kb=id_bytes(pair_key(uint32_t(id),0)); auto k=val(kb); MDB_val v;
    entity_record out;
    int rc=mdb_cursor_get(raw,&k,&v,MDB_SET_RANGE);
    while(rc==0) {
      reader r{{static_cast<char*>(k.mv_data),k.mv_size}}; auto key=r.u64(); r.end();
      if((key>>32)!=id) break;
      out.emplace(scope->name_of(uint32_t(key)),decode_fact(key,{static_cast<char*>(v.mv_data),v.mv_size}));
      rc=mdb_cursor_get(raw,&k,&v,MDB_NEXT);
    }
    if(rc!=0&&rc!=MDB_NOTFOUND) check(rc);
    return out;
  }
  void validate_query(const query& q) {
    if(q.all) { if(!q.clauses.empty()||!q.projection.empty()) throw std::invalid_argument("all view cannot have clauses"); return; }
    if(q.clauses.empty()||q.clauses.size()>32||q.projection.empty()) throw std::invalid_argument("query requires 1..32 clauses and a projection");
    for(const auto& c:q.clauses) {
      if(c.attribute.empty()||c.attribute.size()>1024) throw std::invalid_argument("query requires a fixed attribute");
      for(auto* t:{&c.entity,&c.value}) if(t->variable < -1 || t->variable>=64) throw std::invalid_argument("query variable outside 0..63");
      if(c.entity.variable<0) {
        if(c.entity.constant.size()!=9 || c.entity.constant[0]!=6) throw std::invalid_argument("entity constant must be a reference");
        reader r{std::string_view(c.entity.constant).substr(1)}; valid_entity(r.u64());
      }
    }
    for(auto i:q.projection) if(i>=q.clauses.size()) throw std::invalid_argument("invalid projection");
  }
  bool evaluate(const compiled_query& q,query_result& result,std::optional<uint32_t> only_entity={},bool track=false) {
    // Used only for proven entity-local queries. Initial subscription and error
    // recovery always run the full evaluator, with no entity restriction.
    bytes restricted_entity;
    if(only_entity) {
      if(!q.entity_local) throw std::logic_error("partial evaluation of nonlocal query");
      restricted_entity=entity_value(*only_entity);
      const auto& entity=q.clauses.front().entity;
      if(entity.variable<0 && entity.constant!=restricted_entity) return false;
    }
    auto& output=*result.data_;
    bool changed=false;
    dense::set<uint64_t> seen;
    // Local queries can only project these attributes of the changed entity.
    // This avoids an O(view size) scan for one entity's incremental update.
    auto has_local_entity=[&] {
      for(auto i:q.projection)
        if(output.facts.contains(pair_key(*only_entity,q.clauses[i].attribute))) return true;
      return false;
    };
    const bool had_entity=track&&only_entity&&has_local_entity();
    dense::set<uint32_t> previous_entities;
    if(track&&!only_entity)
      for(const auto& [key,value]:output.facts) previous_entities.insert(uint32_t(key>>32));
    // Index entries own boxes for the duration of this owner-thread evaluation.
    struct match { uint32_t entity=0,attribute=0; const value_ref* value=nullptr; };
    [[maybe_unused]] auto apply=[&](const match& t) {
      DF_COUNT(emitted);
      const auto key=pair_key(t.entity,t.attribute); seen.insert(key);
      const auto& value=*t.value;
      auto [old,inserted]=output.facts.try_emplace(key,value);
      if(!inserted && value_equal{}(old->second,value)) return;
      old->second=value; changed=true;
      if(track) {
        output.changes.entities.insert(t.entity);
        output.changes.attributes.insert(t.attribute);
      }
      DF_COUNT(query_tree_writes);
    };
#ifdef DATAFRIEND_BUFFER_QUERIES
    DF_START(query_match_ns);
    std::vector<match> buffered; buffered.reserve(only_entity?q.projection.size():output.facts.size());
    auto emit=[&](const match& t) { buffered.push_back(t); };
#else
    auto& emit=apply;
#endif
    if(q.all) {
      for(const auto& [a,es]:aev) for(const auto& [id,v]:es) emit({id,a,&v});
    } else {
      std::array<const bytes*,64> bound{};
      std::array<const value_ref*,64> bound_box{};
      std::array<match,32> matched{};
      // Resolve attribute maps once per evaluation; index mutation has finished.
      std::array<const entities*,32> columns{};
      for(size_t i=0;i<q.clauses.size();++i) {
        auto it=aev.find(q.clauses[i].attribute);
        if(it!=aev.end()) columns[i]=&it->second;
      }
      if(only_entity && q.entities.front().entity.variable>=0)
        bound[q.entities.front().entity.variable]=&restricted_entity;
      auto resolved=[&](const term& t)->const bytes* {
        return t.variable<0? &t.constant : bound[t.variable];
      };
      struct access_path {
        size_t count=std::numeric_limits<size_t>::max();
        uint32_t anchor=0;
        std::optional<uint32_t> entity;
        const dense::set<uint32_t>* ids=nullptr;
      };
      auto access=[&](const entity_group& g) {
        access_path best;
        // A bound entity is a single point gather, never an attribute scan.
        if(auto e=resolved(g.entity)) {
          best.count=0;
          if(e->size()!=9 || (*e)[0]!=6) return best;
          reader r{std::string_view(*e).substr(1)}; auto id=r.u64();
          if(id>UINT32_MAX) return best;
          best.count=1; best.entity=uint32_t(id); return best;
        }
        for(auto i:g.checks) {
          if(!columns[i]) return access_path{0,0,{},nullptr};
          const auto& c=q.clauses[i];
          access_path candidate; candidate.anchor=i;
          if(auto v=resolved(c.value)) {
            auto av=ave.find(c.attribute);
            if(av==ave.end()) return access_path{0,0,{},nullptr};
            auto box=c.value.variable<0?c.constant:bound_box[c.value.variable]?
              *bound_box[c.value.variable]:std::make_shared<const boxed_value>(*v);
            auto values=av->second.find(box);
            if(values==av->second.end()) return access_path{0,0,{},nullptr};
            candidate.ids=&values->second; candidate.count=values->second.size();
          } else candidate.count=columns[i]->size();
          if(candidate.count<best.count) best=candidate;
        }
        return best;
      };
      // A small greedy planner uses current exact bucket sizes. Group order can
      // change with bound reference/value variables, without stale cached costs.
      auto walk=[&](auto&& self,uint32_t remaining)->void {
        if(!remaining) { for(auto i:q.projection) emit(matched[i]); return; }
        uint32_t selected=0; access_path path;
        for(uint32_t g=0;g<q.entities.size();++g) if(remaining&(uint32_t(1)<<g)) {
          auto candidate=access(q.entities[g]);
          if(!candidate.count) return;
          if(candidate.count<path.count) { selected=g; path=candidate; }
        }
        const auto& group=q.entities[selected];
        auto consider=[&](uint32_t id) {
          const auto entity=entity_value(id);
          std::array<int32_t,64> added; size_t added_count=0;
          auto bind=[&](const term& t,const bytes& value,const value_ref* box=nullptr) {
            if(t.variable<0) return t.constant==value;
            auto& b=bound[t.variable]; if(b) return *b==value;
            b=&value; bound_box[t.variable]=box; added[added_count++]=t.variable; return true;
          };
          bool matches=bind(group.entity,entity);
          // All these fields belong to one entity. Gather/test them in a tight
          // loop; only joins between entity groups recurse.
          for(auto i:group.checks) {
            if(!matches) break;
            if(!columns[i]) { matches=false; break; }
            auto value=columns[i]->find(id);
            if(value==columns[i]->end()) { matches=false; break; }
            DF_COUNT(candidates);
            const auto& c=q.clauses[i];
            if(c.value.variable<0 || c.bind_value)
              matches=bind(c.value,value->second->value,&value->second);
            if(c.projected) matched[i]={id,c.attribute,&value->second};
          }
          if(matches) self(self,remaining&~(uint32_t(1)<<selected));
          for(size_t i=0;i<added_count;++i) {
            bound[added[i]]=nullptr; bound_box[added[i]]=nullptr;
          }
        };
        if(path.entity) consider(*path.entity);
        else if(path.ids) for(auto id:*path.ids) consider(id);
        else for(const auto& [id,value]:*columns[path.anchor]) consider(id);
      };
      const auto groups=q.entities.size();
      walk(walk,groups==32?UINT32_MAX:(uint32_t(1)<<groups)-1);

    }
#ifdef DATAFRIEND_BUFFER_QUERIES
    DF_STOP(query_match_ns);
    DF_START(query_view_ns);
    for(const auto& t:buffered) apply(t);
#endif
    // unordered_dense erases by moving the last element; inspect that slot again.
    auto prune=[&](auto fact) {
      DF_COUNT(previous_entries);
      if(seen.contains(fact->first)) return ++fact;
      if(track) {
        output.changes.entities.insert(uint32_t(fact->first>>32));
        output.changes.attributes.insert(uint32_t(fact->first));
      }
      changed=true;
      return output.facts.erase(fact);
    };
    if(only_entity) {
      // A missing match removes only this entity's previous projection. Other
      // rows must not be scanned or pruned against this evaluation's seen set.
      for(auto i:q.projection) {
        auto fact=output.facts.find(pair_key(*only_entity,q.clauses[i].attribute));
        if(fact!=output.facts.end()) prune(fact);
      }
      if(track) output.changes.membership |= had_entity!=has_local_entity();
    } else {
      for(auto fact=output.facts.begin();fact!=output.facts.end();) fact=prune(fact);
      if(track&&changed) {
        dense::set<uint32_t> current_entities;
        for(const auto& [key,value]:output.facts) current_entities.insert(uint32_t(key>>32));
        output.changes.membership |= previous_entities!=current_entities;
      }
    }
    return changed;
  }

  using fact_changes=std::map<uint64_t,std::optional<bytes>>;
  std::vector<uint64_t> commit(fact_changes changes) {
    writable(); recomputed=0;
    uint64_t counter=next_entity;
    DF_START(stage_ns);
    transaction tx(env);
    for(auto it=changes.begin();it!=changes.end();) {
      const auto key=it->first;
      scope->name_of(uint32_t(key));
      const auto* old=current(key);
      if((!old&&!it->second)||(old&&it->second&&(*old)->value==*it->second)) {
        it=changes.erase(it); continue;
      }
      auto kb=id_bytes(key); auto k=val(kb);
      if(it->second) {
        auto encoded=encode_fact(key,*it->second); auto v=val(encoded);
        check(mdb_put(tx.p,db,&k,&v,0));
      } else check(mdb_del(tx.p,db,&k,nullptr));
      counter=std::max(counter,(key>>32)+1);
      ++it;
    }
    if(counter!=next_entity) write_counter(tx.p,counter);
    DF_STOP(stage_ns);
    if(changes.empty()) return {};
    // On the rare failure path rebuild indexes from committed EAV. Normal edits
    // mutate only affected entities, never copy the entire database/indexes.
    try {
      DF_START(indexes_ns);
      dense::map<query_group*,dense::set<uint32_t>> affected;
      for(auto* group:wildcard) affected.try_emplace(group);
      auto visit=[&](const auto& groups,uint32_t entity) {
        for(auto* group:groups) {
          auto& ids=affected[group];
          if(group->q.entity_local) ids.insert(entity);
        }
      };
#if defined(DATAFRIEND_BATCH_ROUTING) && defined(DATAFRIEND_DIRECT_SUBSCRIPTIONS)
      struct link_change { query_group* group; uint32_t entity; bool present; };
      std::vector<link_change> links;
      {
        DF_START(routing_lookup_ns);
        // All lookups use the old indexes plus the complete normalized batch.
        // A changed predicate cannot still equal its old constant: staging has
        // removed no-ops. Old and new buckets therefore yield disjoint link edits.
        for(const auto& [key,value]:changes) {
          const auto attribute=uint32_t(key), entity=uint32_t(key>>32);
          if(auto it=subscribers.find(attribute);it!=subscribers.end()) visit(it->second,entity);
          if(auto it=fixed_subscribers.find(key);it!=fixed_subscribers.end()) visit(it->second,entity);
          if(auto it=direct_subscribers.find(key);it!=direct_subscribers.end()) {
            visit(it->second,entity);
            for(auto* group:it->second) {
              const auto& predicate=group->q.clauses[*group->q.route_predicate];
              if(predicate.attribute==attribute) links.push_back({group,entity,false});
            }
          }
          if(!value) continue;
          auto a=match_subscribers.find(attribute);
          if(a==match_subscribers.end()) continue;
          auto bucket=a->second.find(routing_value{*value,hash(*value)});
          if(bucket==a->second.end()) continue;
          visit(bucket->second,entity);
          for(auto* group:bucket->second) links.push_back({group,entity,true});
        }
      }
      { DF_START(index_update_ns); for(const auto& [key,value]:changes) index(key,value); }
      {
        DF_START(routing_maintenance_ns);
        for(const auto& link:links) link_candidate(link.group,link.entity,link.present);
      }
#elif defined(DATAFRIEND_BATCH_ROUTING)
      {
        DF_START(routing_lookup_ns);
        struct predicate_state {
          uint32_t attribute;
          const value_ref* before;
          std::optional<routing_value> after;
        };
        // Packed keys put one entity's edits together. Reuse predicate reads and
        // pending-value hashes across all changed attributes of that entity.
        std::vector<predicate_state> cached;
        std::optional<uint32_t> previous_entity;
        for(const auto& [key,value]:changes) {
          const auto attribute=uint32_t(key), entity=uint32_t(key>>32);
          if(previous_entity!=entity) { cached.clear(); previous_entity=entity; }
          if(auto it=subscribers.find(attribute);it!=subscribers.end()) visit(it->second,entity);
          if(auto it=fixed_subscribers.find(key);it!=fixed_subscribers.end()) visit(it->second,entity);
          auto predicates=predicate_subscribers.find(attribute);
          if(predicates==predicate_subscribers.end()) continue;
          for(const auto& [predicate,values]:predicates->second) {
            auto state=std::find_if(cached.begin(),cached.end(),[&](const auto& s){return s.attribute==predicate;});
            if(state==cached.end()) {
              const auto predicate_key=pair_key(entity,predicate);
              auto pending=changes.find(predicate_key);
              std::optional<routing_value> after;
              // An unchanged predicate needs just its old lookup; a deletion
              // needs no new bucket. Only changed, present values add a lookup.
              if(pending!=changes.end()&&pending->second)
                after=routing_value{*pending->second,hash(*pending->second)};
              cached.push_back({predicate,current(predicate_key),after});
              state=std::prev(cached.end());
            }
            if(state->before) {
              auto bucket=values.find(*state->before);
              if(bucket!=values.end()) visit(bucket->second,entity);
            }
            if(state->after) {
              auto bucket=values.find(*state->after);
              if(bucket!=values.end()) visit(bucket->second,entity);
            }
          }
        }
      }
      { DF_START(index_update_ns); for(const auto& [key,value]:changes) index(key,value); }
#elif defined(DATAFRIEND_DIRECT_SUBSCRIPTIONS)
      dense::map<query_group*,dense::set<uint32_t>> refresh;
      {
        DF_START(routing_old_ns);
        for(const auto& [key,value]:changes) {
          const auto attribute=uint32_t(key), entity=uint32_t(key>>32);
          if(auto it=subscribers.find(attribute);it!=subscribers.end()) visit(it->second,entity);
          if(auto it=fixed_subscribers.find(key);it!=fixed_subscribers.end()) visit(it->second,entity);
          if(auto it=direct_subscribers.find(key);it!=direct_subscribers.end()) {
            visit(it->second,entity);
            for(auto* group:it->second) {
              const auto& predicate=group->q.clauses[*group->q.route_predicate];
              if(predicate.attribute==attribute) refresh[group].insert(entity);
            }
          }
        }
      }
      { DF_START(index_update_ns); for(const auto& [key,value]:changes) index(key,value); }
      {
        DF_START(routing_new_ns);
        // Only predicate edits can create new candidates. Read the complete new
        // state, then add dependencies for every clause, including absent facts.
        for(const auto& [key,value]:changes) {
          auto a=match_subscribers.find(uint32_t(key));
          if(a==match_subscribers.end()) continue;
          const auto* now=current(key);
          if(!now) continue;
          auto v=a->second.find(*now);
          if(v==a->second.end()) continue;
          const auto entity=uint32_t(key>>32);
          visit(v->second,entity);
          for(auto* group:v->second) refresh[group].insert(entity);
        }
      }
      {
        DF_START(routing_maintenance_ns);
        for(const auto& [group,entities]:refresh) {
          const auto& predicate=group->q.clauses[*group->q.route_predicate];
          for(auto entity:entities) {
            const auto* now=current(pair_key(entity,predicate.attribute));
            link_candidate(group,entity,now&&value_equal{}(*now,predicate.constant));
          }
        }
      }
#else
      auto route=[&](bool predicates_only) {
        for(const auto& [key,value]:changes) {
          const auto attribute=uint32_t(key), entity=uint32_t(key>>32);
          // General joins keep conservative invalidation, including variable
          // clauses on the far side of a fixed-entity join. Local queries carry
          // only relevant IDs instead of the transaction's Cartesian product.
          if(!predicates_only) {
            if(auto it=subscribers.find(attribute);it!=subscribers.end()) visit(it->second,entity);
            if(auto it=fixed_subscribers.find(key);it!=fixed_subscribers.end()) visit(it->second,entity);
          }
          auto predicates=predicate_subscribers.find(attribute);
          if(predicates==predicate_subscribers.end()) continue;
          for(const auto& [predicate,values]:predicates->second) {
            const auto* required=current(pair_key(entity,predicate));
            if(!required) continue;
            if(auto bucket=values.find(*required);bucket!=values.end()) visit(bucket->second,entity);
          }
        }
      };
      // Read both complete states, never intermediate per-fact states. Routing
      // on old values removes departed members; new values add arriving members.
      { DF_START(routing_old_ns); route(false); }
      { DF_START(index_update_ns); for(const auto& [key,value]:changes) index(key,value); }
      { DF_START(routing_new_ns); route(true); }
#endif
      DF_STOP(indexes_ns);
      DF_START(queries_ns);
      std::set<query_group*> updated;
      for(const auto& [group,entities]:affected) {
        group->result.data_->changes.clear();
        bool changed_result=false;
        if(group->q.entity_local) {
          for(const auto id:entities)
            changed_result=evaluate(group->q,group->result,id,true) || changed_result;
        } else changed_result=evaluate(group->q,group->result,{},true);
        if(changed_result) updated.insert(group);
      }
      std::vector<uint64_t> notifications;
      for(const auto& [id,group]:views) if(updated.contains(group)) notifications.push_back(id);
      DF_STOP(queries_ns);
      DF_START(commit_ns);
#ifdef DATAFRIEND_TESTING
      if(std::exchange(fail_commit,false)) throw std::runtime_error("injected commit failure");
#endif
      tx.commit(); // the blocked application resumes only after all work completes
      written();
      next_entity=counter;
      DF_STOP(commit_ns);
      DF_START(publish_ns);
      ++version; recomputed=affected.size();
      DF_STOP(publish_ns);
      return notifications;
    } catch(...) {
      // Abort before reopening a read transaction (also avoids stale read state).
      if(tx.p) mdb_txn_abort(std::exchange(tx.p,nullptr));
      try {
        rebuild();
#ifdef DATAFRIEND_DIRECT_SUBSCRIPTIONS
        rebuild_candidates();
#endif
        for(auto& [key,group]:groups) {
          auto& result=*group.result.data_;
          result.facts.clear();
          result.changes.clear(); result.changes.reset=true;
          evaluate(group.q,group.result);
        }
      } catch(...) { gate->poisoned=true; }
      throw;
    }
  }
};
engine::engine(const std::string& d,durability mode,size_t bytes,std::string name):p_(std::make_unique<impl>(d,mode,bytes,std::move(name))) {}
engine::engine(const std::string& d,bool durable,size_t bytes,std::string name):engine(d,durable?durability::synchronous:durability::buffered,bytes,std::move(name)) {}
engine::~engine()=default;
uint64_t engine::subscribe(query q) { return p_->call([&]{
  p_->healthy(); p_->validate_query(q);
  if(!p_->next_view) throw std::overflow_error("view IDs exhausted");
  const auto id=p_->next_view;
  auto key=query_key(q);
  if(auto it=p_->groups.find(key);it!=p_->groups.end()) {
    // Identical queries share the live map without another evaluation.
    p_->views.emplace(id,&it->second);
    ++it->second.owners; ++p_->next_view;
    return id;
  }
  auto compiled=p_->compile(q);
  query_result result(p_->scope,p_->gate);
  p_->evaluate(compiled,result);
  auto [position,inserted]=p_->groups.emplace(key,impl::query_group{key,std::move(compiled),std::move(result),0});
  auto* group=&position->second;
  try {
    if(group->q.all) p_->wildcard.insert(group);
#ifdef DATAFRIEND_DIRECT_SUBSCRIPTIONS
    if(group->q.route_predicate) {
      const auto& predicate=group->q.clauses[*group->q.route_predicate];
      p_->match_subscribers[predicate.attribute][predicate.constant].insert(group);
      p_->seed_candidates(group);
    } else
#endif
    for(const auto& c:group->q.clauses) {
#ifndef DATAFRIEND_DIRECT_SUBSCRIPTIONS
      if(group->q.route_predicate) {
        const auto& predicate=group->q.clauses[*group->q.route_predicate];
        p_->predicate_subscribers[c.attribute][predicate.attribute][predicate.constant].insert(group);
      } else
#endif
      if(c.fixed_dependency) p_->fixed_subscribers[*c.fixed_dependency].insert(group);
      else p_->subscribers[c.attribute].insert(group);
    }
    p_->views.emplace(id,group);
  } catch(...) { p_->erase_group(group); throw; }
  group->owners=1; ++p_->next_view;
  return id;
}); }
void engine::unsubscribe(uint64_t id) { p_->call([&]{
  auto it=p_->views.find(id); if(it==p_->views.end()) return;
  auto* group=it->second; p_->views.erase(it);
  if(!--group->owners) p_->erase_group(group);
}); }
query_result engine::result(uint64_t id) { return p_->call([&]{p_->healthy(); return p_->views.at(id)->result;}); }
std::vector<uint64_t> engine::modify(uint64_t id,edit changes) {
  p_->call([&]{ p_->healthy(); p_->views.at(id); });
  return modify(std::move(changes));
}
std::vector<uint64_t> engine::modify_many(std::vector<std::pair<uint64_t,edit>> batches) {
  edit delta;
  p_->call([&]{
    p_->healthy();
    for(auto& [id,changes]:batches) {
      p_->views.at(id);
      delta.remove.insert(delta.remove.end(),changes.remove.begin(),changes.remove.end());
      delta.add.insert(delta.add.end(),changes.add.begin(),changes.add.end());
    }
  });
  return modify(std::move(delta));
}
std::vector<uint64_t> engine::modify(edit delta) { return p_->call([&]{
#ifdef DATAFRIEND_PROFILE
  profile_session session(p_->profile);
#endif
  DF_START(owner_ns);
  p_->healthy();
  for(const auto& t:delta.remove) valid(t);
  for(const auto& t:delta.add) valid(t);
  impl::fact_changes changes;
  { DF_START(read_ns);
    for(const auto& t:delta.remove) {
      auto key=pair_key(uint32_t(t.entity),p_->intern_attribute(t.attribute));
      auto pending=changes.find(key);
      if(pending!=changes.end()) {
        if(pending->second&&*pending->second==t.value) pending->second.reset();
      } else if(const auto* old=p_->current(key);old&&(*old)->value==t.value) changes.emplace(key,std::nullopt);
    }
    for(auto& t:delta.add) changes[pair_key(uint32_t(t.entity),p_->intern_attribute(t.attribute))]=std::move(t.value);
  }
  return p_->commit(std::move(changes));
}); }
uint64_t engine::new_entity() { return p_->call([&]{
  p_->writable(); if(p_->next_entity>UINT32_MAX) throw std::overflow_error("entity IDs exhausted in namespace");
  auto id=p_->next_entity;
  transaction tx(p_->env); p_->write_counter(tx.p,id+1); tx.commit(); p_->written();
  p_->next_entity=id+1; return id;
}); }
void engine::flush() {
  if(p_->flusher) p_->flusher->flush();
  else p_->call([&]{check(mdb_env_sync(p_->env,1));});
}
std::vector<uint64_t> engine::import_facts(std::map<uint64_t,std::optional<bytes>> input) { return p_->call([&]{
  for(auto& [key,record]:input) if(record) record=decode_fact(key,*record);
  return p_->commit(std::move(input));
}); }
std::map<uint64_t,bytes> engine::export_facts() { return p_->call([&]{
  p_->healthy(); std::map<uint64_t,bytes> out; transaction tx(p_->env,MDB_RDONLY);
  MDB_cursor* raw; check(mdb_cursor_open(tx.p,p_->db,&raw));
  std::unique_ptr<MDB_cursor,decltype(&mdb_cursor_close)> c(raw,mdb_cursor_close);
  MDB_val k,v; int rc;
  while((rc=mdb_cursor_get(c.get(),&k,&v,MDB_NEXT))==0) {
    reader r{{static_cast<char*>(k.mv_data),k.mv_size}}; auto key=r.u64(); r.end();
    out.emplace(key,bytes(static_cast<char*>(v.mv_data),v.mv_size));
  }
  if(rc!=MDB_NOTFOUND) check(rc);
  return out;
}); }
std::vector<uint64_t> engine::import_entities(std::map<uint64_t,std::optional<bytes>> input) { return p_->call([&]{
  p_->healthy(); impl::fact_changes changes;
  { transaction tx(p_->env,MDB_RDONLY);
    for(const auto& [id,b]:input) {
      valid_entity(id);
      auto next=b?decode_entity(id,*b):entity_record{};
      auto before=p_->read(tx.p,id);
      for(const auto& [name,value]:before) if(!next.contains(name))
        changes[pair_key(uint32_t(id),p_->intern_attribute(name))]=std::nullopt;
      for(auto& [name,value]:next)
        changes[pair_key(uint32_t(id),p_->intern_attribute(name))]=std::move(value);
    }
  }
  return p_->commit(std::move(changes));
}); }
std::map<uint64_t,bytes> engine::export_entities() {
  const auto facts=export_facts(); std::map<uint64_t,entity_record> records;
  for(const auto& [key,value]:facts) records[key>>32][p_->scope->name_of(uint32_t(key))]=decode_fact(key,value);
  std::map<uint64_t,bytes> out;
  for(const auto& [id,record]:records) out.emplace(id,encode_entity(id,record));
  return out;
}
#ifdef DATAFRIEND_TESTING
void engine::fail_next_commit_for_testing() { p_->call([&]{p_->fail_commit=true;}); }
void engine::fail_flush_for_testing(bool fail) { p_->fail_flush.store(fail); }
uint64_t engine::flush_attempts_for_testing() { return p_->flusher?p_->flusher->attempts():0; }
#endif
uint64_t engine::revision() { return p_->call([&]{return p_->version;}); }
bytes engine::sync(unsigned operation,std::string_view packet) { return p_->call([&]() -> bytes {
  if(operation==4) { p_->network.reset(); return {}; }
  p_->healthy();
  if(operation==0||operation==5) {
    p_->network.reset();
    p_->network=std::make_unique<sync_snapshot>(p_->env,p_->db,p_->version);
    if(operation==0) return lb_sync::begin(p_->network->source());
    operation=1; // Server supplied the opening packet; we still collect differences.
  }
  if(!p_->network) throw std::logic_error("no active sync");
  auto& s=*p_->network;
  if(operation==1) {
    if(s.complete) throw std::logic_error("sync already complete");
    auto reply=lb_sync::receive(s.source(),reconciliation::role::initiator,packet,&s.have,&s.need);
    s.complete=reply.size()==4; return reply;
  }
  if(!s.complete) throw std::logic_error("sync is not complete");
  if(!s.prepared) {
    auto ids=s.have; ids.insert(s.need.begin(),s.need.end());
    s.outgoing.assign(ids.begin(),ids.end()); s.fetching.assign(s.need.begin(),s.need.end()); s.prepared=true;
  }
  writer w;
  if(operation==2) {
    // Same-key content changes are puts, never a put followed by a delete.
    writer records; uint32_t count=0;
    while(s.push_offset<s.outgoing.size()&&records.data.size()<256*1024) {
      auto id=s.outgoing[s.push_offset++]; ++count;
      records.u64(id); records.u8(s.have.contains(id)); if(s.have.contains(id)) records.blob(s.record(id));
    }
    w.u32(count); w.data+=records.data;
  } else if(operation==3) {
    auto count=std::min(size_t(64),s.fetching.size()-s.fetch_offset);
    w.u8(s.fetch_offset+count==s.fetching.size());
    w.u32(uint32_t(count)); for(size_t i=0;i<count;++i) w.u64(s.fetching[s.fetch_offset++]);
  } else throw std::invalid_argument("unknown sync operation");
  if(w.data.size()>lb_sync::max_frame-2048) throw std::length_error("sync transfer exceeds frame limit");
  return w.data;
}); }
std::vector<uint64_t> engine::sync_apply(std::string_view packet) { return p_->call([&]{
  if(!p_->network||!p_->network->complete) throw std::logic_error("no completed sync");
  auto& s=*p_->network;
  if(s.revision!=p_->version) throw std::runtime_error("local data changed during pull; retry");
  reader r{packet}; auto count=r.count(12);
  impl::fact_changes batch;
  for(uint32_t i=0;i<count;++i) {
    auto id=r.u64(); auto record=r.blob();
    if(!s.need.contains(id)||s.incoming.contains(id)||batch.contains(id)) throw std::invalid_argument("unexpected or duplicate transferred fact");
    batch[id]=decode_fact(id,record);
  }
  r.end(); s.incoming.merge(batch);
  if(s.incoming.size()!=s.need.size()) {
    if(!count) throw std::invalid_argument("incomplete fact transfer");
    return std::vector<uint64_t>{};
  }
  auto changes=std::move(s.incoming);
  for(auto id:s.have) if(!s.need.contains(id)) changes[id]=std::nullopt;
  p_->network.reset(); return p_->commit(std::move(changes));
}); }
uint64_t engine::last_recomputed() { return p_->call([&]{return p_->recomputed;}); }
#ifdef DATAFRIEND_PROFILE
edit_profile engine::last_profile() { return p_->call([&]{return p_->profile;}); }
#endif
} // namespace datafriend
