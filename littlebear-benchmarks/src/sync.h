#pragma once
#include "storage.h"
#include "index_sync.h"
#include "exchange.h"
#include "task.h"
#include <map>
#include <optional>
#include <cstring>

namespace lb_sync {
// One storage owner. Transport/authentication is the driver's responsibility. Every
// request re-looks-up its namespace; no table header survives across requests.
struct state {
  storage::store cache{};
  uint64_t epoch = 0;
  uint64_t generation=0;
  std::map<std::string,uint64_t> revisions;
  std::set<std::string> writers;
  state() = default;
  state(const state&) = delete;
  ~state() { storage::destroy(&cache); }
};
struct transfer_guard {
  std::set<std::string>& active; const std::string& name; uint64_t& generation;
  transfer_guard(std::set<std::string>& a,const std::string& n,uint64_t& g):active(a),name(n),generation(g) { active.insert(name); }
  ~transfer_guard() { active.erase(name); ++generation; }
};
inline void check(storage::result r) {
  if(r.code!=storage::error::ok) throw std::runtime_error("Littlebear storage operation failed");
}
inline void perform(storage::request& request) {
  for(;;) {
    storage::io_operation op{}; check(storage::start_io(&request,&op));
    auto n=op.writing?pwrite(op.fd,op.buffer,op.length,op.offset):pread(op.fd,op.buffer,op.length,op.offset);
    auto result=storage::complete_io(&request,n<0?-errno:int(n));
    if(result.code!=storage::error::more_io) { check(result); return; }
  }
}
inline void create(state* s, const char* path, uint64_t epoch, uint64_t capacity=uint64_t(1)<<30) {
  check(storage::create(&s->cache,path,capacity)); s->epoch=epoch;
}
inline uint64_t generation(const state* s) { return s->generation; }
inline uint64_t revision(const state* s, const std::string& name) {
  const auto found=s->revisions.find(name); return found==s->revisions.end()?0:found->second;
}
inline bool writing(const state* s, const std::string& name) { return s->writers.contains(name); }
inline std::string opening(state* s, const std::string& name,uint8_t status=0) {
  hash_table::key ns{};
  if(name.size()!=sizeof ns) throw std::invalid_argument("invalid namespace digest");
  std::memcpy(&ns,name.data(),sizeof ns);
  auto* tree=hash_table::get(&s->cache.namespaces,ns);
  writer out;
  out.u8(writing(s,name)?1:tree?status:3); out.u64(s->epoch); out.u64(lb_sync::revision(s,name));
  if(tree&&!writing(s,name)) out.data+=begin(index_sync::source(tree));
  return out.data;
}
// op 0 start; 1 reconcile; 2 fetch; 3 apply. Reply status 0 success,
// 1 stale/restart required, 2 malformed/storage failure (driver converts throw).
inline response_task handle_async(state* s, unsigned op,std::string name,bool writable,uint64_t epoch,uint64_t revision,std::string payload) {
  if(name.size()!=16) throw std::invalid_argument("invalid internal namespace digest");
  if(op==3&&!writable) throw std::invalid_argument("owner authorization required");
  hash_table::key ns{}; std::memcpy(&ns,name.data(),sizeof ns);
  const bool absent=storage::lookup_namespace(&s->cache,ns)==storage::presence::absent;
  const auto found=s->revisions.find(name);
  uint64_t version=found==s->revisions.end()?0:found->second;
  writer out;
  if(s->writers.contains(name)) { out.u8(1); out.u64(s->epoch); out.u64(version); co_return out.data; }
  if(op==0) {
    reader start{payload}; auto push=start.u8(); start.end();
    if(push>1) throw std::invalid_argument("invalid sync mode");
    if(absent) {
      out.u8(3); out.u64(s->epoch); out.u64(version); co_return out.data;
    }
    out.u8(0); out.u64(s->epoch); out.u64(version); co_return out.data;
  }
  if(epoch!=s->epoch||revision!=version||(absent&&op==2)) co_return opening(s,name,1);
  std::string reply;
  if(op==1) {
    record_index::header empty{};
    auto* tree=hash_table::get(&s->cache.namespaces,ns);
    reply=receive(index_sync::source(tree?tree:&empty),reconciliation::role::responder,payload);
  } else if(op==2) {
    reader r{payload}; auto count=r.count(8);
    if(count>max_records) throw std::length_error("too many records");
    std::set<uint64_t> ids;
    for(uint32_t i=0;i<count;++i) if(!ids.insert(r.u64()).second) throw std::invalid_argument("duplicate key");
    r.end(); writer records; records.u32(count);
    for(auto id:ids) {
      if(lb_sync::revision(s,name)!=version||writing(s,name)) co_return opening(s,name,1);
      std::string buffer(65535,'\0'); storage::request request{};
      auto prepared=storage::prepare_read(&s->cache,&request,ns,id,buffer.data(),buffer.size());
      if(prepared.code==storage::error::busy) co_return opening(s,name,1);
      check(prepared); co_await file_io{request};
      if(lb_sync::revision(s,name)!=version||writing(s,name)) co_return opening(s,name,1);
      records.u64(id); records.blob({buffer.data(),request.transferred});
      if(records.data.size()>max_frame-32) throw std::length_error("transfer exceeds frame limit");
    }
    reply=std::move(records.data);
  } else if(op==3) {
    transfer_guard busy(s->writers,name,s->generation);
    reader r{payload}; auto count=r.count(9);
    if(count>max_records) throw std::length_error("too many records");
    std::map<uint64_t,std::optional<std::string>> records;
    // Validate the complete batch before publishing any mutation.
    for(uint32_t i=0;i<count;++i) {
      auto id=r.u64(); auto present=r.u8(); if(present>1) throw std::invalid_argument("invalid presence flag");
      std::optional<std::string> record;
      if(present) {
        record=r.blob();
        if(record->size()<8||record->size()>65535) throw std::invalid_argument("invalid fact size");
        writer hashed; hashed.u64(id); hashed.data.append(*record,8,std::string::npos);
        if(integer(*record)!=XXH64(hashed.data.data(),hashed.data.size(),0)) throw std::invalid_argument("invalid fact fingerprint");
      }
      if(!records.emplace(id,std::move(record)).second) throw std::invalid_argument("duplicate fact");
    }
    r.end();
    // Remote coherence across a failed I/O batch is deliberately eventual.
    // Invalidate ALL old exchanges even if a later storage write fails.
    if(absent) check(storage::create_namespace(&s->cache,ns));
    if(absent||!records.empty()) s->revisions[name]=++version;
    for(auto& [id,record]:records) {
      if(record) {
        storage::request request{};
        auto prepared=storage::prepare_write(&s->cache,&request,ns,id,record->data(),record->size(),integer(*record));
        if(prepared.code==storage::error::busy) co_return opening(s,name,1);
        check(prepared);
        co_await file_io{request};
      } else {
        auto result=storage::erase_record(&s->cache,ns,id);
        if(result.code==storage::error::busy) co_return opening(s,name,1);
        if(result.code!=storage::error::record_missing) check(result);
      }
    }
  } else throw std::invalid_argument("unknown sync operation");
  out.u8(0); out.u64(s->epoch); out.u64(version); out.data+=reply;
  co_return out.data;
}
// Portable synchronous driver for deterministic component tests. The Linux
// transport submits the same storage requests through its io_uring loop.
inline std::string handle(state* s, unsigned op,const std::string& name,bool writable,uint64_t epoch,uint64_t revision,std::string_view payload) {
  auto task=handle_async(s,op,name,writable,epoch,revision,std::string(payload));
  while(!task.done()) {
    task.resume();
    if(auto* request=task.pending()) perform(*request);
  }
  return task.result();
}
}
