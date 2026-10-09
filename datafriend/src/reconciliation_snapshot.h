#pragma once
#include "exchange.h"
#include "lmdb.h"
#include <map>

namespace datafriend {
// Created, queried and destroyed on the same serialized calling context as the engine.
// LMDB's read transaction supplies MVCC; no copied fact tree is built.
struct sync_snapshot {
  MDB_txn* tx=nullptr;
  MDB_dbi db;
  uint64_t revision;
  std::set<uint64_t> have,need;
  bool complete=false;
  bool prepared=false;
  std::vector<uint64_t> outgoing, fetching;
  size_t push_offset=0,fetch_offset=0;
  std::map<uint64_t,std::optional<bytes>> incoming;
  sync_snapshot(MDB_env* env,MDB_dbi d,uint64_t rev):db(d),revision(rev) {
    if(int rc=mdb_txn_begin(env,nullptr,MDB_RDONLY,&tx)) throw std::runtime_error(mdb_strerror(rc));
  }
  ~sync_snapshot() { if(tx) mdb_txn_abort(tx); }
  static MDB_val value(std::string_view b) { return {b.size(),const_cast<char*>(b.data())}; }
  static bool summary(void* ctx,reconciliation::range part,reconciliation::summary* out) {
    auto& s=*static_cast<sync_snapshot*>(ctx);
    writer lo,hi; lo.u64(part.low); hi.u64(part.high);
    auto low=value(lo.data),high=value(hi.data); MDB_agg aggregate{};
    if(mdb_agg_range(s.tx,s.db,&low,nullptr,part.high?&high:nullptr,nullptr,MDB_RANGE_LOWER_INCL,&aggregate)) return false;
    *out={aggregate.mv_agg_entries,lb_sync::integer({reinterpret_cast<char*>(aggregate.mv_agg_hashes),8})};
    return true;
  }
  static bool select(void* ctx,uint64_t rank,uint64_t* out) {
    auto& s=*static_cast<sync_snapshot*>(ctx); MDB_val k{},v{};
    if(mdb_agg_select(s.tx,s.db,MDB_AGG_WEIGHT_ENTRIES,rank,&k,&v,nullptr)||k.mv_size!=8) return false;
    *out=lb_sync::integer({static_cast<char*>(k.mv_data),k.mv_size}); return true;
  }
  static size_t list(void* ctx,reconciliation::range part,reconciliation::item* out,size_t capacity) {
    auto& s=*static_cast<sync_snapshot*>(ctx); MDB_cursor* c=nullptr;
    if(mdb_cursor_open(s.tx,s.db,&c)) return SIZE_MAX;
    writer key; key.u64(part.low); auto k=value(key.data); MDB_val v{};
    int rc=mdb_cursor_get(c,&k,&v,MDB_SET_RANGE); size_t n=0;
    while(!rc&&n<capacity) {
      if(k.mv_size!=8||v.mv_size<8) { n=SIZE_MAX; break; }
      auto id=lb_sync::integer({static_cast<char*>(k.mv_data),k.mv_size});
      if(part.high&&id>=part.high) break;
      out[n++]={id,lb_sync::integer({static_cast<char*>(v.mv_data),v.mv_size})};
      rc=mdb_cursor_get(c,&k,&v,MDB_NEXT);
    }
    mdb_cursor_close(c); return rc&&rc!=MDB_NOTFOUND?SIZE_MAX:n;
  }
  reconciliation::source source() { return {this,summary,select,list}; }
  std::string record(uint64_t id) {
    writer key; key.u64(id); auto k=value(key.data); MDB_val v{};
    if(int rc=mdb_get(tx,db,&k,&v)) throw std::runtime_error(mdb_strerror(rc));
    return {static_cast<char*>(v.mv_data),v.mv_size};
  }
};
}
