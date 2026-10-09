#pragma once
#include <ankerl/unordered_dense.h>
#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <stdexcept>
#include <map>
#include <functional>
#include "attributes.h"

#ifdef DATAFRIEND_PROFILE
#include "profiling.h"
#endif

namespace datafriend {
using bytes = std::string;
struct triple {
  uint64_t entity;
  std::string attribute;
  bytes value; // canonical typed bytes; query engine compares byte-for-byte
  bool operator==(const triple&) const = default;
};
// Boxes are immutable and shared by indexes and live results, including scalars.
struct boxed_value {
  const bytes value;
  const uint64_t hash;
  explicit boxed_value(bytes);
};
using value_ref = std::shared_ptr<const boxed_value>;
struct value_hash { size_t operator()(const value_ref& v) const noexcept { return v->hash; } };
struct value_equal {
  bool operator()(const value_ref& a,const value_ref& b) const noexcept {
    return a==b || a->value==b->value;
  }
};
// Every scope uses the same generated attribute IDs. Handles retain names after
// engine close; there is no per-store attribute allocation or persisted dictionary.
class view_scope {
  std::map<std::string,uint32_t,std::less<>> ids_;
  std::map<uint32_t,std::string> names_;
  uint64_t next_=1;
  void define(uint32_t,std::string);
 public:
  const std::string name;
  explicit view_scope(std::string n="default"):name(std::move(n)) {
    for(const auto& a:attribute_definitions) define(a.id,std::string(a.name));
  }
  std::optional<uint32_t> find(std::string_view) const;
  std::string name_of(uint32_t) const;
  uint64_t next_id() const;
  uint32_t intern(std::string); // resolves a name in the shared schema; never allocates an ID
};
uint64_t pair_key(uint32_t entity,uint32_t attribute) noexcept;
uint64_t triple_hash(uint64_t key,const boxed_value&);
// One live result per distinct query. Copies share a mutable packed-key map.
// The caller serializes reads and blocking edits on one application thread.
class query_result {
 public:
  struct entry { uint64_t key; value_ref value; };
  struct access { bool poisoned=false; };
  struct change_summary {
    ankerl::unordered_dense::set<uint32_t> entities, attributes;
    bool membership=false, reset=true;
    void clear() { entities.clear(); attributes.clear(); membership=false; reset=false; }
  };
  struct storage {
    ankerl::unordered_dense::map<uint64_t,value_ref> facts;
    change_summary changes;
    std::shared_ptr<view_scope> scope;
    std::shared_ptr<access> gate;
    storage(std::shared_ptr<view_scope> s,std::shared_ptr<access> g):scope(std::move(s)),gate(std::move(g)) {}
  };
 private:
  std::shared_ptr<storage> data_;
  void check() const {
    if(data_->gate->poisoned) throw std::runtime_error("Datafriend recovery failed; reopen store");
  }
  friend class engine;
 public:
  explicit query_result(std::shared_ptr<view_scope> s=std::make_shared<view_scope>(),
                        std::shared_ptr<access> g=std::make_shared<access>()):data_(std::make_shared<storage>(std::move(s),std::move(g))) {}
  auto scope() const { return data_->scope; }
  bool shares_with(const query_result& other) const { return data_==other.data_; }
  uint64_t size() const { check(); return data_->facts.size(); }
  bool empty() const { return size()==0; }
  const change_summary& changes() const { check(); return data_->changes; }
  std::optional<entry> get(uint64_t key) const {
    check(); auto it=data_->facts.find(key);
    if(it==data_->facts.end()) return {};
    return entry{key,it->second};
  }
  auto lookup(uint32_t entity,std::string_view attribute) const {
    auto a=scope()->find(attribute);
    return a?get(pair_key(entity,*a)):std::optional<entry>{};
  }
  triple decode(const entry& e) const { return {e.key>>32,scope()->name_of(uint32_t(e.key)),e.value->value}; }
  // Callback must not call back into this result or the engine.
  template<class F> void for_each(F&& f) const {
    check();
    for(const auto& [key,v]:data_->facts) f(entry{key,v});
  }
  // Borrow valid only until the next mutation/close. Decode immediately on caller thread.
  const bytes* value(uint32_t entity,uint32_t attribute) const {
    check(); auto it=data_->facts.find(pair_key(entity,attribute));
    return it==data_->facts.end()?nullptr:&it->second->value;
  }
  // Ascending integer fields, missing last for each field, then entity ID.
  // With no sort fields, order by entity ID alone.
  std::vector<uint32_t> entities(uint32_t order=0,uint32_t secondary=0) const;
  // Scan packed keys for one entity; no values are copied.
  std::vector<std::string> attribute_names(uint32_t entity) const {
    check(); std::vector<std::string> names;
    for(const auto& [key,value]:data_->facts)
      if(uint32_t(key>>32)==entity) names.push_back(data_->scope->name_of(uint32_t(key)));
    return names;
  }
  std::optional<uint32_t> find_entity(uint32_t attribute,std::string_view value) const {
    check();
    for(const auto& [key,v]:data_->facts)
      if(uint32_t(key)==attribute&&v->value==value) return uint32_t(key>>32);
    return {};
  }
  uint64_t count_value(uint32_t attribute,std::string_view value) const {
    check(); uint64_t count=0;
    for(const auto& [key,v]:data_->facts)
      if(uint32_t(key)==attribute&&v->value==value) ++count;
    return count;
  }
  bytes encode() const; // count + triples; order unspecified
};
using entity_record = std::map<std::string, bytes>; // canonical attribute ordering
struct term {
  int32_t variable = -1; // -1 = constant, otherwise a query-local variable
  bytes constant;
};
struct clause { term entity; std::string attribute; term value; };
struct query {
  std::vector<clause> clauses;
  std::vector<uint32_t> projection; // indices of actual matched clauses
  bool all = false; // explicit all-facts view, also useful for bootstrap/admin
};
struct edit { std::vector<triple> remove, add; };
bytes entity_value(uint64_t entity); // type tag 6 + big endian uint64 (reference)
bytes encode_entity(uint64_t entity, const entity_record&);
entity_record decode_entity(uint64_t entity, std::string_view);
// Attribute-level storage/transport record: XXH64(key || value), then value bytes.
bytes encode_fact(uint64_t key,std::string_view value);
bytes decode_fact(uint64_t key,std::string_view record);
enum class durability { buffered=0, synchronous=1, asynchronous=2 };
// Edits synchronously commit and publish on a private owner thread. In the
// default asynchronous mode, a separate worker coalesces disk flushes (100 ms).
// Returned handles share live results; the flush worker never accesses them.
class engine {
  struct impl;
  std::unique_ptr<impl> p_;
 public:
  explicit engine(const std::string& directory, durability mode = durability::asynchronous, size_t map_bytes = size_t(1)<<30, std::string namespace_name="default");
  engine(const std::string& directory, bool durable, size_t map_bytes = size_t(1)<<30, std::string namespace_name="default");
  ~engine();
  engine(const engine&) = delete;
  engine& operator=(const engine&) = delete;
  uint64_t subscribe(query);
  void unsubscribe(uint64_t view);
  query_result result(uint64_t view);
  std::vector<uint64_t> modify(edit changes);
  // Compatibility convenience: the view is validated, edits still target the database.
  std::vector<uint64_t> modify(uint64_t view, edit changes);
  std::vector<uint64_t> modify_many(std::vector<std::pair<uint64_t,edit>> changes);
  uint64_t new_entity();
  // Explicit blocking durability barrier, also used by checked close. Edits do
  // not call this. Flush failures are retried and reject new writes until fixed.
  void flush();
  // Attribute-level transport boundary. Keys are packed entity/attribute pairs;
  // missing payload means an actual deletion. One import is one transaction.
  std::vector<uint64_t> import_facts(std::map<uint64_t, std::optional<bytes>> records);
  std::map<uint64_t, bytes> export_facts();
  // Whole-entity convenience helpers for document export/replacement/deletion.
  // These do not describe the on-disk or network unit.
  std::vector<uint64_t> import_entities(std::map<uint64_t, std::optional<bytes>> records);
  std::map<uint64_t, bytes> export_entities();
  uint64_t revision();
  // Network commands run on the owner; one stable AELMDB read transaction per
  // exchange. 0 begin, 1 receive, 2 next push page, 3 next fetch page (final-page
  // flag followed by count/IDs), 4 cancel. These are transport plumbing.
  bytes sync(unsigned operation, std::string_view packet={});
  // Stage a fetched page; publish once all expected facts have arrived.
  std::vector<uint64_t> sync_apply(std::string_view records);
  // Distinct query groups evaluated by the most recent edit, not handle count.
  uint64_t last_recomputed();
#ifdef DATAFRIEND_TESTING
  void fail_next_commit_for_testing();
  void fail_flush_for_testing(bool);
  uint64_t flush_attempts_for_testing();
#endif
#ifdef DATAFRIEND_PROFILE
  edit_profile last_profile();
#endif
};
} // namespace datafriend
