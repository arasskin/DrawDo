#include "reconcile.h"
#include "btree_index.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <sys/resource.h>
#ifdef __APPLE__
#include <mach/mach.h>
#else
#include <unistd.h>
#endif

using namespace experiment;
using clock_type = std::chrono::steady_clock;
uint64_t now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now().time_since_epoch()).count();
}
size_t resident() {
#ifdef __APPLE__
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t size = MACH_TASK_BASIC_INFO_COUNT;
  require(task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &size)
          == KERN_SUCCESS, "resident memory measurement");
  return info.resident_size;
#else
  auto* file = std::fopen("/proc/self/statm", "r");
  require(file != nullptr, "open statm");
  unsigned long virtual_pages = 0, resident_pages = 0;
  const int fields = std::fscanf(file, "%lu %lu", &virtual_pages, &resident_pages);
  std::fclose(file); require(fields == 2, "parse statm");
  return resident_pages * size_t(sysconf(_SC_PAGESIZE));
#endif
}
uint64_t key_for(size_t i, const std::string& distribution, uint64_t seed) {
  if (distribution == "uniform") return permute(i + seed*UINT64_C(0x9e3779b97f4a7c15));
  if (distribution == "sequential") return i;
  if (distribution == "clustered") return ((uint64_t(i)/256) << 32) | (i%256);
  throw std::runtime_error("unknown key distribution");
}
using metrics = std::map<std::string, double>;

struct allocation_tape {
  struct event { size_t id,bytes; bool allocate; };
  std::vector<event> events;
  std::unordered_map<void*,size_t> live;
  size_t next=0;
};
struct traced_nodes {
  allocation_tape* tape;
  void* allocate(size_t bytes) {
    void* p=std::malloc(bytes); if(!p) std::abort();
    const auto id=tape->next++;
    tape->live.emplace(p,id); tape->events.push_back({id,bytes,true}); return p;
  }
  void deallocate(void* p,size_t bytes) {
    tape->events.push_back({tape->live.at(p),bytes,false}); tape->live.erase(p); std::free(p);
  }
};
// Replay actual tree allocation lifetimes without tree traversal/copying. The
// trace is prepared before timing. First/last bytes are touched and verified.
template<class Allocator> metrics allocation_replay(size_t n,uint64_t seed) {
  allocation_tape tape;
  {
    datafriend::basic_wide_tree<traced_nodes> tree(key_hash,traced_nodes{&tape});
    for(size_t i=0;i<n;++i) tree.set(make_record(key_for(i,"uniform",seed),i+1));
    std::vector<size_t> order(n); std::iota(order.begin(),order.end(),0);
    std::mt19937_64 rng(seed); std::shuffle(order.begin(),order.end(),rng);
    for(size_t i=0;i<n/2;++i) tree.erase(key_for(order[i],"uniform",seed));
    for(size_t i=0;i<n/2;++i) tree.set(make_record(key_for(order[i],"uniform",seed),i+7));
  }
  require(tape.live.empty(),"trace leak");
  std::vector<void*> pointers(tape.next);
  Allocator allocator;
  metrics m{{"allocation_requests",tape.next},{"allocator_calls",tape.events.size()}};
  for(unsigned pass=0;pass<4;++pass) {
    const auto start=now();
    for(const auto& e:tape.events) {
      if(e.allocate) {
        auto* p=static_cast<uint8_t*>(allocator.allocate(e.bytes));
        require(p!=nullptr,"replay allocation"); pointers[e.id]=p;
        p[0]=uint8_t(e.id); p[e.bytes-1]=uint8_t(e.id>>8);
      } else {
        auto* p=static_cast<uint8_t*>(pointers[e.id]);
        require(p[0]==uint8_t(e.id) && p[e.bytes-1]==uint8_t(e.id>>8),"replay corruption");
        allocator.deallocate(p,e.bytes);
      }
    }
    m["pass_"+std::to_string(pass)+"_ns_per_call"]=double(now()-start)/tape.events.size();
  }
  if constexpr(std::is_same_v<Allocator,datafriend::slab_nodes>) {
    const auto& s=allocator.pool->stats();
    m["pool_peak_bytes"]=s.peak_reserved_bytes;
    m["retained_after_clear"]=s.reserved_bytes;
    m["slab_allocations"]=s.slab_allocations;
    m["slab_frees"]=s.slab_frees;
    m["fallback_allocations"]=s.fallback_allocations;
    require(s.live_bytes==0,"replay slab leak"); allocator.pool->trim();
    require(allocator.pool->stats().reserved_bytes==0,"replay trim leak");
  }
  return m;
}
size_t reserved(const record_index& t) { return t.allocations().live_bytes; }
size_t reserved(const binary_index& t) { return t.reserved_slot_bytes(); }
size_t logical(const record_index& t) { return t.allocations().live_bytes; }
size_t reserved(const slab_index& t) { return t.allocator().pool->stats().reserved_bytes; }
size_t logical(const slab_index& t) { return t.allocations().live_bytes; }
size_t logical(const binary_index& t) { return t.size() ? (2*t.size()-1)*24 : 0; }
template<class T> size_t reserved(const btree_index<T>& t) { return t.reserved_bytes(); }
template<class T> size_t logical(const btree_index<T>& t) { return t.reserved_bytes(); }
template<class Index> std::unique_ptr<Index> create(bool scan) {
  if constexpr (std::is_same_v<Index, binary_index>) return std::make_unique<Index>(scan);
  else return std::make_unique<Index>();
}

template<class Index> metrics micro(size_t n, uint64_t seed, const std::string& distribution) {
  std::vector<record> records, updates;
  for (size_t i = 0; i < n; ++i) {
    const auto k = key_for(i, distribution, seed);
    records.push_back(make_record(k, i+1)); updates.push_back(make_record(k, i+1001));
  }
  std::mt19937_64 rng(seed);
  const size_t queries = std::max<size_t>(200000, n*2);
  std::vector<uint64_t> hits, misses;
  for (size_t i = 0; i < queries; ++i) {
    hits.push_back(records[rng()%n].key);
    misses.push_back(key_for(n+i, distribution, seed));
  }
  std::vector<key_range> ranges;
  for (size_t i = 0; i < 12000; ++i) {
    auto a = hits[rng()%queries], b = hits[rng()%queries];
    if (a > b) std::swap(a,b);
    ranges.push_back({a,b});
  }
  metrics m;
  const auto baseline_rss = resident();
  auto index = create<Index>(false);
  auto start = now();
  for (auto e : records) index->set(e);
  m["insert_ns"] = double(now()-start)/n;
  const auto built_rss = resident();
  m["rss_build_delta"] = double(built_rss)-double(baseline_rss);
  m["logical_bytes"] = logical(*index); m["reserved_bytes"] = reserved(*index);
  m["bytes_per_record"] = double(reserved(*index))/n;
  uint64_t expected = 0;
  for (auto e : records) expected ^= e.hash;
  if constexpr (std::is_same_v<Index,btree_original>) expected=Index::expected_hash(records);
  require(expected == index->fingerprint(), "initial tree checksum");
  uint64_t checksum = 0;
  for (size_t i = 0; i < 10000; ++i) checksum += index->get(hits[i])->value; // warmup
  start = now();
  for (auto k : hits) { const auto e = index->get(k); require(e.has_value(), "lookup miss"); checksum += e->value; }
  m["hit_ns"] = double(now()-start)/queries;
  start = now();
  for (auto k : misses) require(!index->get(k), "unexpected hit");
  m["miss_ns"] = double(now()-start)/queries;
  start = now();
  for (auto r : ranges) checksum ^= index->range_hash(r);
  m["range_ns"] = double(now()-start)/ranges.size();
  start = now();
  for (auto e : updates) require(!index->set(e), "update inserted");
  m["replace_ns"] = double(now()-start)/n;
  std::vector<size_t> deletion_order(n);
  std::iota(deletion_order.begin(), deletion_order.end(), 0);
  std::shuffle(deletion_order.begin(), deletion_order.end(), rng);
  start = now();
  for (size_t i = 0; i < n/2; ++i) require(index->erase(updates[deletion_order[i]].key), "delete failed");
  m["erase_ns"] = double(now()-start)/(n/2);
  m["half_reserved_bytes"] = reserved(*index);
  m["half_live_bytes"] = logical(*index);
  start = now();
  for (size_t i = 0; i < n/2; ++i) index->set(updates[deletion_order[i]]);
  m["reinsert_ns"] = double(now()-start)/(n/2);
  m["refill_reserved_bytes"] = reserved(*index);
  m["rss_after_churn_delta"] = double(resident())-double(baseline_rss);
  std::sort(updates.begin(), updates.end(), [](auto a, auto b) { return a.key < b.key; });
  require(index->list({}, SIZE_MAX) == updates, "post-churn records differ");
  if constexpr (std::is_same_v<Index, record_index> || std::is_same_v<Index, slab_index>) {
    index->validate();
    m["allocations"] = index->allocations().allocations;
    m["copied_bytes"] = index->allocations().copied_bytes;
    m["peak_requested_bytes"] = index->allocations().peak_bytes;
  }
  if constexpr(std::is_same_v<Index,slab_index>) {
    const auto& s=index->allocator().pool->stats();
    m["pool_peak_bytes"]=s.peak_reserved_bytes;
    m["slab_allocations"]=s.slab_allocations;
    m["slab_frees"]=s.slab_frees;
    m["fallback_allocations"]=s.fallback_allocations;
  }
  m["checksum"] = checksum % 1000000007;
  return m;
}

template<class Server> metrics sync(size_t n, uint64_t seed, const std::string& distribution,
                                   const std::string& scenario, bool scan) {
  std::conditional_t<std::is_same_v<Server,btree_original>,btree_original,record_index> client;
  auto server = create<Server>(scan);
  std::vector<uint64_t> keys;
  for (size_t i = 0; i < n; ++i) keys.push_back(key_for(i, distribution, seed));
  auto edit_order = keys;
  if (scenario == "clustered") std::sort(edit_order.begin(), edit_order.end());
  else { std::mt19937_64 rng(seed); std::shuffle(edit_order.begin(), edit_order.end(), rng); }
  const size_t edits = std::max<size_t>(2,n/100);
  const bool editing = scenario == "sparse" || scenario == "clustered";
  std::map<uint64_t, size_t> edited;
  if (editing) for (size_t i = 0; i < edits; ++i) edited[edit_order[i]] = i;
  std::set<uint64_t> expected;
  for (size_t i = 0; i < n; ++i) {
    const auto k = keys[i]; const auto base = make_record(k, i+1);
    if (scenario != "server-empty") server->set(base);
    if (scenario == "client-empty") { expected.insert(k); continue; }
    if (scenario == "disjoint") {
      const auto other = make_record(key_for(n+i, distribution, seed), i+1);
      client.set(other); expected.insert(k); expected.insert(other.key); continue;
    }
    const auto it = edited.find(k);
    if (it == edited.end()) client.set(base);
    else {
      expected.insert(k);
      if (it->second%2 == 0) client.set(make_record(k, i+7)); // replacement
      // odd entries are deleted from the authoritative client
    }
    if (scenario == "server-empty") expected.insert(k);
  }
  if (editing) for (size_t i = 0; i < edits/2; ++i) {
    const auto k = key_for(n+i, distribution, seed);
    client.set(make_record(k, i+999)); expected.insert(k);
  }
  const auto cpu_start = std::clock();
  const auto start = now();
  const auto result = reconciliation::run(client, *server);
  const auto elapsed = now()-start;
  const auto cpu_elapsed = std::clock()-cpu_start;
  require(result.differences == expected, "reconciliation diff oracle mismatch");
  const auto apply_start = now();
  for (auto k : result.differences) {
    if (auto e = client.get(k)) server->set(*e); else server->erase(k);
  }
  const auto apply_elapsed = now()-apply_start;
  require(client.list({}, SIZE_MAX) == server->list({}, SIZE_MAX), "post-sync convergence");
  return {
    {"cpu_ms", 1000.0*double(cpu_elapsed)/CLOCKS_PER_SEC},
    {"elapsed_ms", double(elapsed)/1e6}, {"apply_ms", double(apply_elapsed)/1e6},
    {"turns", result.turns}, {"packets", result.packets}, {"wire_bytes", result.wire_bytes},
    {"client_bytes", result.client_bytes}, {"server_bytes", result.server_bytes},
    {"differences", result.differences.size()},
    {"client_hash_queries", result.client_work.hash_queries},
    {"server_hash_queries", result.server_work.hash_queries},
    {"server_split_calls", result.server_work.split_calls},
    {"server_list_calls", result.server_work.list_calls},
  };
}

struct no_pool {};
template<class Index> struct pool_for { using type=typename Index::pool_type; };
template<> struct pool_for<record_index> { using type=no_pool; };

template<class Index> struct namespace_forest {
  typename pool_for<Index>::type pool;
  std::vector<std::unique_ptr<Index>> roots;
  std::unordered_map<uint64_t,size_t> directory;
  void create_root(size_t i) {
    if constexpr(std::is_same_v<Index,record_index>) roots[i]=std::make_unique<Index>();
    else roots[i]=std::make_unique<Index>(pool);
  }
  size_t node_bytes() const {
    if constexpr(std::is_same_v<Index,binary_index>) return pool.reserved_slot_bytes();
    else if constexpr(std::is_same_v<Index,record_index>) {
      size_t n=0;for(const auto& r:roots) if(r) n+=r->allocations().live_bytes;return n;
    } else if constexpr(std::is_same_v<Index,slab_index>) return pool.stats().reserved_bytes;
    else return pool.bytes_lower_bound();
  }
  size_t accounted_bytes() const {
    size_t n=node_bytes()+sizeof(pool)+roots.capacity()*sizeof(roots[0]);
    for(const auto& r:roots) if(r) n+=sizeof(Index);
    return n+directory.size()*sizeof(typename decltype(directory)::value_type)+directory.bucket_count()*sizeof(void*);
  }
};

double percentile(std::vector<double> values,double p) {
  if(values.empty()) return 0;
  std::sort(values.begin(),values.end());
  return values[std::min(values.size()-1,size_t(p*values.size()))];
}

// Single-threaded event-loop-shaped trace. One shared binary arena or B-tree node
// map per process, separate roots per namespace. All engines use the SAME directory.
template<class Index> metrics many(size_t maps,uint64_t seed,const std::string& profile) {
  std::mt19937_64 rng(seed);
  std::vector<std::vector<record>> model(maps);
  size_t total=0;
  for(size_t ns=0;ns<maps;++ns) {
    size_t n;
    if(profile=="tiny") n=std::array<size_t,4>{0,1,8,32}[ns%4];
    else if(profile=="medium") n=100;
    else if(profile=="large") n=1000;
    else if(profile=="skewed") n=ns%100<90 ? 16 : (ns%100<99 ? 256 : 4096);
    else throw std::runtime_error("unknown namespace profile");
    for(size_t i=0;i<n;++i) model[ns].push_back(make_record(key_for(i,"uniform",permute(seed+ns)),ns*10000+i+1));
    total+=n;
  }
  struct operation {uint64_t ns;size_t entity;unsigned type;record next;key_range range;};
  std::vector<operation> trace;
  constexpr size_t events=200000,batch=1000;
  for(size_t i=0;i<events;++i) {
    const auto ns=rng()%10<9 ? rng()%std::max<size_t>(1,maps/10) : rng()%maps;
    const auto& entries=model[ns];
    const auto e=entries.empty()?0:rng()%entries.size();
    const auto k=entries.empty()?0:entries[e].key;
    auto a=k,b=entries.empty()?0:entries[rng()%entries.size()].key;
    if(a>b) std::swap(a,b);
    trace.push_back({permute(ns),e,unsigned(rng()%100),make_record(k,i+UINT64_C(1000000000)),{a,b}});
  }
  const auto base_rss=resident();
  namespace_forest<Index> forest;
  forest.roots.resize(maps);forest.directory.reserve(maps);
  auto start=now();
  for(size_t ns=0;ns<maps;++ns) {
    forest.directory.emplace(permute(ns),ns);forest.create_root(ns);
    for(auto r:model[ns]) forest.roots[ns]->set(r);
  }
  metrics m{{"namespaces",maps},{"records",total},{"build_ms",double(now()-start)/1e6},
    {"rss_build_delta",double(resident())-double(base_rss)},
    {"node_bytes",forest.node_bytes()},{"accounted_bytes",forest.accounted_bytes()},
    {"node_bytes_per_record",double(forest.node_bytes())/total},
    {"accounted_bytes_per_record",double(forest.accounted_bytes())/total}};
  if constexpr(std::is_same_v<Index,record_index> || std::is_same_v<Index,slab_index>)
    for(const auto& root:forest.roots) m["live_node_bytes_build"]+=root->allocations().live_bytes;
  uint64_t checksum=0;
  std::vector<double> batches;
  const auto mixed_start=now();
  for(size_t b=0;b<events;b+=batch) {
    const auto started=now();
    for(size_t j=b;j<b+batch;++j) {
      const auto& op=trace[j];
      const auto ns=forest.directory.at(op.ns);
      auto& index=*forest.roots[ns];auto& expected=model[ns];
      if(expected.empty()) {require(!index.get(op.next.key),"empty namespace unexpectedly populated");continue;}
      if(op.type<60) {
        const auto r=index.get(op.next.key);
        require(r==expected[op.entity],"namespace lookup mismatch");checksum+=r->value;
      } else if(op.type<85) {
        require(!index.set(op.next),"namespace replacement inserted");expected[op.entity]=op.next;
      } else if(op.type<95) checksum^=index.range_hash(op.range);
      else {
        require(index.erase(op.next.key),"namespace erase failed");
        require(index.set(op.next),"namespace reinsertion failed");expected[op.entity]=op.next;
      }
    }
    batches.push_back(double(now()-started)/batch);
  }
  m["mixed_ns_per_event"]=double(now()-mixed_start)/events;
  m["batch_p50_ns_per_event"]=percentile(batches,.50);
  m["batch_p95_ns_per_event"]=percentile(batches,.95);
  m["batch_p99_ns_per_event"]=percentile(batches,.99);
  m["nodes_after_mixed"]=forest.node_bytes();
  std::vector<size_t> order(maps);std::iota(order.begin(),order.end(),0);
  std::shuffle(order.begin(),order.end(),rng);
  start=now();
  for(size_t i=0;i<maps/10;++i) forest.roots[order[i]].reset();
  m["drop_10pct_ms"]=double(now()-start)/1e6;
  m["nodes_after_drop"]=forest.node_bytes();
  start=now();
  for(size_t i=0;i<maps/10;++i) {
    const auto ns=order[i];forest.create_root(ns);
    for(auto r:model[ns]) forest.roots[ns]->set(r);
  }
  m["rebuild_10pct_ms"]=double(now()-start)/1e6;
  m["nodes_after_refill"]=forest.node_bytes();
  for(size_t ns=0;ns<maps;++ns) {
    auto expected=model[ns];std::sort(expected.begin(),expected.end(),[](auto a,auto b){return a.key<b.key;});
    require(forest.roots[ns]->list({},SIZE_MAX)==expected,"namespace oracle mismatch");
  }

  double sync_cpu=0;size_t wire=0,turns=0,differences=0;
  std::vector<double> sync_times,sync_turns;
  const size_t sessions=std::min<size_t>(256,maps);
  for(size_t session=0;session<sessions;++session) {
    const auto ns=order[session];
    std::conditional_t<std::is_same_v<Index,btree_original>,btree_original,record_index> client;
    const auto before=forest.roots[ns]->list({},SIZE_MAX);
    if(session%4!=2) for(auto r:before) client.set(r);
    std::set<uint64_t> expected;
    if(session%4==1) {
      if(!before.empty()) {client.set(make_record(before[0].key,UINT64_C(9000000000)+session));expected.insert(before[0].key);}
      if(before.size()>1) {client.erase(before[1].key);expected.insert(before[1].key);}
      const auto k=key_for(10000000+session,"uniform",permute(seed+ns));
      client.set(make_record(k,session+7));expected.insert(k);
    } else if(session%4==2) {for(auto r:before) expected.insert(r.key);}
    else if(session%4==3) {
      for(auto r:before) expected.insert(r.key);
      forest.roots[ns].reset();forest.create_root(ns);
    }
    auto& server=*forest.roots[ns];
    const auto started=now();const auto cpu=std::clock();
    const auto result=reconciliation::run(client,server);
    sync_cpu+=1000.0*double(std::clock()-cpu)/CLOCKS_PER_SEC;
    sync_times.push_back(double(now()-started)/1e6);sync_turns.push_back(result.turns);
    require(result.differences==expected,"namespace sync missed differences");
    for(auto k:result.differences) {if(auto r=client.get(k)) server.set(*r);else server.erase(k);}
    require(server.list({},SIZE_MAX)==client.list({},SIZE_MAX),"namespace sync did not converge");
    wire+=result.wire_bytes;turns+=result.turns;differences+=result.differences.size();
  }
  m["sync_sessions"]=sessions;m["sync_cpu_ms"]=sync_cpu;m["sync_wire_bytes"]=wire;
  m["sync_turns_total"]=turns;m["sync_differences"]=differences;
  m["sync_p50_ms"]=percentile(sync_times,.5);m["sync_p95_ms"]=percentile(sync_times,.95);
  m["sync_turns_p95"]=percentile(sync_turns,.95);m["checksum"]=checksum%1000000007;
  if constexpr(std::is_same_v<Index,record_index> || std::is_same_v<Index,slab_index>) {
    size_t requests=0,copied=0,live=0;
    for(const auto& root:forest.roots) {
      requests+=root->allocations().allocations;
      copied+=root->allocations().copied_bytes;
      live+=root->allocations().live_bytes;
    }
    m["surviving_roots_allocations"]=requests;
    m["surviving_roots_copied_bytes"]=copied;
    m["live_before_clear"]=live;
    start=now(); forest.roots.clear();
    m["clear_all_ms"]=double(now()-start)/1e6;
    m["retained_after_clear"]=forest.node_bytes();
    if constexpr(std::is_same_v<Index,slab_index>) {
      const auto& s=forest.pool.stats();
      m["pool_peak_bytes"]=s.peak_reserved_bytes;
      m["slab_allocations"]=s.slab_allocations;
      m["slab_frees"]=s.slab_frees;
      m["fallback_allocations"]=s.fallback_allocations;
      require(s.live_bytes==0,"slab live allocation leak");
      forest.pool.trim();
      m["retained_after_trim"]=forest.pool.stats().reserved_bytes;
      require(forest.pool.stats().reserved_bytes==0,"slab reservation leak");
    }
  }
  return m;
}

int main(int argc, char** argv) {
  try {
    require(argc == 7, "usage: compare alloc|micro|sync|many binary|binary-scan|wide|slab|btree|btree-original COUNT SEED uniform|sequential|clustered SCENARIO");
    const std::string mode = argv[1], engine = argv[2], distribution = argv[5], scenario = argv[6];
    const auto count = std::stoull(argv[3]), seed = std::stoull(argv[4]);
    require(count >= 64 && count <= 10000000, "count must be 64..10000000");
    require(engine == "binary" || engine == "binary-scan" || engine == "wide" || engine=="slab" || engine=="btree" || engine=="btree-original", "unknown engine");
    // A long-running server pays crypto initialization once. Exclude that
    // process startup cost from operation/sync timers and memory deltas.
    if (engine == "btree-original") (void)btree_original::expected_hash({});
    metrics m;
    if(mode=="alloc") {
      require(engine=="wide" || engine=="slab","allocator replay supports wide/slab");
      m=engine=="slab" ? allocation_replay<datafriend::slab_nodes>(count,seed)
                        : allocation_replay<datafriend::malloc_nodes>(count,seed);
    } else if (mode == "micro") {
      require(engine != "binary-scan", "scan policy applies only to sync");
      m = engine == "slab" ? micro<slab_index>(count,seed,distribution)
        : engine == "btree" ? micro<btree_xor>(count,seed,distribution)
        : engine == "btree-original" ? micro<btree_original>(count,seed,distribution)
        : engine == "wide" ? micro<record_index>(count,seed,distribution)
                           : micro<binary_index>(count,seed,distribution);
    } else if(mode=="many") {
      m = engine=="slab" ? many<slab_index>(count,seed,scenario)
        : engine=="btree" ? many<btree_xor>(count,seed,scenario)
        : engine=="btree-original" ? many<btree_original>(count,seed,scenario)
        : engine=="wide" ? many<record_index>(count,seed,scenario)
        : many<binary_index>(count,seed,scenario);
    } else {
      require(mode == "sync", "unknown mode");
      require(scenario == "identical" || scenario == "sparse" || scenario == "clustered" ||
              scenario == "client-empty" || scenario == "server-empty" || scenario == "disjoint", "unknown scenario");
      m = engine == "slab" ? sync<slab_index>(count,seed,distribution,scenario,false)
        : engine == "btree" ? sync<btree_xor>(count,seed,distribution,scenario,false)
        : engine == "btree-original" ? sync<btree_original>(count,seed,distribution,scenario,false)
        : engine == "wide" ? sync<record_index>(count,seed,distribution,scenario,false)
                           : sync<binary_index>(count,seed,distribution,scenario,engine == "binary-scan");
    }
    std::cout << std::setprecision(15) << "{\"mode\":\"" << mode << "\",\"engine\":\"" << engine
              << "\",\"count\":" << count << ",\"seed\":" << seed
              << ",\"distribution\":\"" << distribution << "\",\"scenario\":\"" << scenario << '"';
    for (const auto& [name, value] : m) std::cout << ",\"" << name << "\":" << value;
    std::cout << "}\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
