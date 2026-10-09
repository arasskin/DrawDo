#include "datafriend/c_api.h"
#include "datafriend/engine.h"
#include "datafriend/codec.h"
#include <cstring>
using namespace datafriend;
namespace {
void copy(df_buffer* out,std::string_view b) {
  void* p=std::malloc(b.size()); if(!p&&!b.empty()) throw std::bad_alloc{};
  if(!b.empty()) std::memcpy(p,b.data(),b.size());
  out->data=static_cast<uint8_t*>(p); out->size=b.size();
}
void error(df_buffer* out,const char* s) noexcept { try { copy(out,s); } catch(...) {} }
triple read_triple(reader& r) { auto id=r.u64(); auto a=r.blob(); auto v=r.blob(); return {id,std::move(a),std::move(v)}; }
void write_triple(writer& w,const triple& t) { w.u64(t.entity); w.blob(t.attribute); w.blob(t.value); }
void write_changes(writer& w,const std::vector<uint64_t>& ids) { w.u32(uint32_t(ids.size())); for(auto id:ids) w.u64(id); }
term read_term(reader& r) { auto tag=r.u8(); if(tag==0) return {-1,r.blob()}; if(tag==1) return {int32_t(r.u8()),{}}; throw std::invalid_argument("invalid query term"); }
}
extern "C" {
void* df_alloc(size_t size) { return std::malloc(size); }
void df_free(void* p) { std::free(p); }
int df_attribute_schema(df_buffer* out) {
  *out={}; try { copy(out,attribute_schema); return 0; }
  catch(const std::exception& e) { error(out,e.what()); return 1; }
}
void* df_open_namespace(const uint8_t* path,size_t n,const uint8_t* name,size_t name_size,int durable,df_buffer* out) {
  *out={}; try {
    if(!path||!n||n>32768||std::memchr(path,0,n)||!name||!name_size||name_size>1024) throw std::invalid_argument("invalid directory or namespace");
    return new engine(std::string(reinterpret_cast<const char*>(path),n),static_cast<durability>(durable),size_t(1)<<30,
                      std::string(reinterpret_cast<const char*>(name),name_size));
  } catch(const std::exception& e) { error(out,e.what()); return nullptr; }
}
void* df_open(const uint8_t* path,size_t n,int durable,df_buffer* out) {
  return df_open_namespace(path,n,reinterpret_cast<const uint8_t*>("default"),7,durable,out);
}
void df_close(void* p) { delete static_cast<engine*>(p); }
int df_close_checked(void* p,df_buffer* out) {
  *out={};
  std::unique_ptr<engine> db(static_cast<engine*>(p));
  try { if(db) db->flush(); return 0; }
  catch(const std::exception& e) { error(out,e.what()); return 1; }
}
int df_call(void* p,const uint8_t* input,size_t size,df_buffer* out) {
  *out={}; try {
    if(!p||!input||size>64*1024*1024) throw std::invalid_argument("invalid request");
    reader r{{reinterpret_cast<const char*>(input),size}}; if(r.u8()!=1) throw std::invalid_argument("unsupported API version");
    auto op=r.u8(); auto& db=*static_cast<engine*>(p); writer w;
    // Mutating calls return at most an ID. Allocate their reply before commit.
    if(op==1||op==5) { out->data=static_cast<uint8_t*>(std::malloc(8)); if(!out->data) throw std::bad_alloc{}; w.data.reserve(8); }
    switch(op) {
      case 1: { query q; auto all=r.u8(); if(all>1) throw std::invalid_argument("invalid all flag"); q.all=all;
        auto n=r.count(7); for(uint32_t i=0;i<n;++i) { auto e=read_term(r); auto a=r.blob(); auto v=read_term(r); q.clauses.push_back({std::move(e),std::move(a),std::move(v)}); }
        n=r.count(4); for(uint32_t i=0;i<n;++i) q.projection.push_back(r.u32()); r.end(); w.u64(db.subscribe(std::move(q))); break; }
      case 2: { auto id=r.u64(); r.end(); w.data=db.result(id).encode(); break; }
      case 3: { auto id=r.u64(); edit changes; auto n=r.count(16); for(uint32_t i=0;i<n;++i) changes.remove.push_back(read_triple(r)); n=r.count(16); for(uint32_t i=0;i<n;++i) changes.add.push_back(read_triple(r)); r.end(); write_changes(w,db.modify(id,std::move(changes))); break; }
      case 4: { auto id=r.u64(); r.end(); db.unsubscribe(id); break; }
      case 5: { r.end(); w.u64(db.new_entity()); break; }
      case 6: { r.end(); auto records=db.export_entities(); w.u32(uint32_t(records.size())); for(const auto& [id,b]:records) { w.u64(id); w.blob(b); } break; }
      case 7: { std::map<uint64_t,std::optional<bytes>> records; auto n=r.count(9); for(uint32_t i=0;i<n;++i) { auto id=r.u64(); auto present=r.u8(); if(present>1) throw std::invalid_argument("invalid record flag"); std::optional<bytes> b; if(present) b=r.blob(); if(!records.emplace(id,std::move(b)).second) throw std::invalid_argument("duplicate entity"); } r.end(); write_changes(w,db.import_entities(std::move(records))); break; }
      case 8: { std::vector<std::pair<uint64_t,edit>> batches; auto n=r.count(16);
        for(uint32_t i=0;i<n;++i) { auto id=r.u64(); edit e; auto count=r.count(16);
          for(uint32_t j=0;j<count;++j) e.remove.push_back(read_triple(r));
          count=r.count(16); for(uint32_t j=0;j<count;++j) e.add.push_back(read_triple(r));
          batches.emplace_back(id,std::move(e)); }
        r.end(); write_changes(w,db.modify_many(std::move(batches))); break; }
      case 9: { auto id=r.u64(); auto entity=r.u64(); auto attribute=r.blob(); r.end();
        if(entity>UINT32_MAX) throw std::invalid_argument("entity ID exceeds namespace uint32 range");
        auto tree=db.result(id); auto entry=tree.lookup(uint32_t(entity),attribute);
        w.u8(entry.has_value()); if(entry) write_triple(w,tree.decode(*entry)); break; }
      case 10: { edit changes; auto n=r.count(16);
        for(uint32_t i=0;i<n;++i) changes.remove.push_back(read_triple(r));
        n=r.count(16); for(uint32_t i=0;i<n;++i) changes.add.push_back(read_triple(r));
        r.end(); write_changes(w,db.modify(std::move(changes))); break; }
      case 11: { r.end(); db.flush(); break; }
      case 12: { r.end(); auto records=db.export_facts(); w.u32(uint32_t(records.size()));
        for(const auto& [key,b]:records) { w.u64(key); w.blob(b); } break; }
      case 13: { std::map<uint64_t,std::optional<bytes>> records; auto n=r.count(9);
        for(uint32_t i=0;i<n;++i) { auto key=r.u64(); auto present=r.u8();
          if(present>1) throw std::invalid_argument("invalid record flag");
          std::optional<bytes> b; if(present) b=r.blob();
          if(!records.emplace(key,std::move(b)).second) throw std::invalid_argument("duplicate fact"); }
        r.end(); write_changes(w,db.import_facts(std::move(records))); break; }
      case 14: { auto command=r.u8(); auto packet=r.blob(); r.end(); w.data=db.sync(command,packet); break; }
      case 15: { auto packet=r.blob(); r.end(); write_changes(w,db.sync_apply(packet)); break; }
      default: throw std::invalid_argument("unknown operation");
    }
    if(out->data) { std::memcpy(out->data,w.data.data(),w.data.size()); out->size=w.data.size(); }
    else if(!w.data.empty()) copy(out,w.data);
    return 0;
  } catch(const std::exception& e) { df_free(out->data); *out={}; error(out,e.what()); return 1; }
}
void* df_result(void* p,uint64_t id,df_buffer* out) {
  *out={}; try { if(!p) throw std::invalid_argument("closed engine"); return new query_result(static_cast<engine*>(p)->result(id)); }
  catch(const std::exception& e) { error(out,e.what()); return nullptr; }
}
int df_result_info(void* p,df_buffer* out) {
  *out={}; try { writer w; w.u64(static_cast<query_result*>(p)->size()); copy(out,w.data); return 0;
  } catch(const std::exception& e) { error(out,e.what()); return 1; }
}
int df_result_same(void* a,void* b) { return static_cast<query_result*>(a)->shares_with(*static_cast<query_result*>(b)); }
int df_result_list(void* p,df_buffer* out) {
  *out={}; try { copy(out,static_cast<query_result*>(p)->encode()); return 0; }
  catch(const std::exception& e) { error(out,e.what()); return 1; }
}
int df_result_changes(void* p,df_buffer* out) {
  *out={}; try {
    const auto& result=*static_cast<query_result*>(p);
    const auto& changes=result.changes(); writer w;
    w.u8(changes.reset); w.u8(changes.membership);
    w.u32(uint32_t(changes.entities.size()));
    for(auto id:changes.entities) w.u32(id);
    w.u32(uint32_t(changes.attributes.size()));
    for(auto id:changes.attributes) w.blob(result.scope()->name_of(id));
    copy(out,w.data); return 0;
  } catch(const std::exception& e) { error(out,e.what()); return 1; }
}
uint32_t df_result_attribute(void* p,const uint8_t* name,size_t size) {
  return static_cast<query_result*>(p)->scope()->find({reinterpret_cast<const char*>(name),size}).value_or(0);
}
int df_result_value(void* p,uint32_t entity,uint32_t attribute,df_buffer* out) {
  *out={}; try {
    auto value=static_cast<query_result*>(p)->value(entity,attribute);
    if(!value) return 1;
    out->data=reinterpret_cast<uint8_t*>(const_cast<char*>(value->data())); out->size=value->size(); return 0;
  } catch(...) { return -1; }
}
int df_result_entities(void* p,uint32_t order,uint32_t secondary,df_buffer* out) {
  *out={}; try {
    auto ids=static_cast<query_result*>(p)->entities(order,secondary);
    if(!ids.empty()) copy(out,{reinterpret_cast<const char*>(ids.data()),ids.size()*sizeof(uint32_t)});
    return 0;
  } catch(const std::exception& e) { error(out,e.what()); return 1; }
}
int64_t df_result_find(void* p,uint32_t attribute,const uint8_t* value,size_t size) {
  try { auto id=static_cast<query_result*>(p)->find_entity(attribute,{reinterpret_cast<const char*>(value),size});
    return id?int64_t(*id):-1;
  } catch(...) { return -2; }
}
int df_result_attributes(void* p,uint32_t entity,df_buffer* out) {
  *out={}; try {
    auto names=static_cast<query_result*>(p)->attribute_names(entity); writer w;
    w.u32(uint32_t(names.size()));
    for(const auto& name:names) w.blob(name);
    copy(out,w.data); return 0;
  } catch(const std::exception& e) { error(out,e.what()); return 1; }
}
int64_t df_result_count_value(void* p,uint32_t attribute,const uint8_t* value,size_t size) {
  try { return static_cast<query_result*>(p)->count_value(attribute,{reinterpret_cast<const char*>(value),size}); }
  catch(...) { return -1; }
}
void df_result_release(void* p) { delete static_cast<query_result*>(p); }
}
