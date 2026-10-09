#include "check.h"
#include "index.h"
#include <map>
#include <random>

namespace {
using datafriend::record;
struct controlled_allocator {
  datafriend::slab_pool* pool;
  size_t* budget;
  void* allocate(size_t bytes) {
    if(*budget!=SIZE_MAX) { if(!*budget) return nullptr; --*budget; }
    return pool->allocate(bytes);
  }
  void deallocate(void* p,size_t bytes) { pool->deallocate(p,bytes); }
};
using test_tree=datafriend::basic_wide_tree<controlled_allocator>;
uint64_t ranks(uint64_t k) { return k==UINT64_MAX ? 0 : record_index::key_hash(k); }
record entry(uint64_t k) { return {k,k^73,record_index::key_hash(k^123)}; }

// Fail each allocation along successful insert/delete paths, including joining
// neighbors and promotion to the maximum rank. Every failure preserves content.
void failures() {
  std::mt19937_64 rng(174);
  std::vector<record> base;
  for(size_t i=0;i<512;++i) base.push_back(entry(rng()));
  base.push_back(entry(UINT64_MAX));
  size_t insert_failures=0,delete_failures=0;
  for(bool removing:{false,true}) for(size_t trial=0;trial<80;++trial) {
    const uint64_t key=trial==0 ? UINT64_MAX : removing ? base[trial].key : rng();
    bool completed=false;
    for(size_t allowed=0;allowed<64 && !completed;++allowed) {
      datafriend::slab_pool pool;
      size_t budget=SIZE_MAX;
      {
        test_tree tree(ranks,controlled_allocator{&pool,&budget});
        for(auto r:base) if(removing || r.key!=key) tree.set(r);
        const auto before=tree.list({},SIZE_MAX);
        const auto shape=tree.shape_digest();
        budget=allowed;
        try {
          if(removing) CHECK(tree.erase(key)); else CHECK(tree.set(entry(key)));
          completed=true;
        } catch(const std::bad_alloc&) {
          CHECK(tree.list({},SIZE_MAX)==before);
          CHECK(tree.shape_digest()==shape);
          (removing ? delete_failures : insert_failures)++;
        }
        tree.validate();
        auto expected=before;
        if(completed) {
          if(removing) std::erase_if(expected,[&](auto r){return r.key==key;});
          else {expected.push_back(entry(key)); std::sort(expected.begin(),expected.end(),[](auto a,auto b){return a.key<b.key;});}
        }
        CHECK(tree.list({},SIZE_MAX)==expected);
      }
      CHECK(pool.stats().live_bytes==0); pool.trim(); CHECK(pool.stats().reserved_bytes==0);
    }
    CHECK(completed);
  }
  CHECK(insert_failures>10 && delete_failures>0);
  std::printf("PASS: failed %zu insert and %zu delete allocations without losing records\n",insert_failures,delete_failures);
}

void model() {
  record_index::pool pool;
  auto h=record_index::create(&pool);
  std::map<uint64_t,record> expected;
  std::mt19937_64 rng(857);
  for(size_t i=0;i<20000;++i) {
    uint64_t k=rng()%2000;
    if(i%13==0) k=UINT64_MAX;
    if(rng()%3) {
      record r{k,rng()|1,rng()};
      if(expected.contains(k)) CHECK(record_index::replace(&h,k,r.value,r.hash));
      else CHECK(record_index::unsafe_associate(&h,k,r.value,r.hash)==record_index::NO_ERRORS);
      expected[k]=r;
    } else CHECK(record_index::erase(&h,k)==(expected.erase(k) ? record_index::erase_result::removed : record_index::erase_result::missing));
    if(i%53==0) {
      record_index::validate(&h);
      CHECK(record_index::size(&h)==expected.size());
      std::vector<record> records; uint64_t hash=0;
      for(auto [key,r]:expected) {records.push_back(r);hash^=r.hash;CHECK(record_index::find(&h,key)==r);}
      CHECK(record_index::list(&h)==records);
      CHECK(record_index::get_full_fingerprint(&h)==hash);
      for(size_t rank=0;rank<records.size();++rank) CHECK(record_index::select(&h,rank)==records[rank]);
      const auto summary=record_index::summarize(&h,{100,1000});
      uint64_t range_hash=0,count=0;
      for(auto [key,r]:expected) if(key>=100 && key<1000) {range_hash^=r.hash;++count;}
      CHECK(summary.hash==range_hash && summary.count==count);
      const auto cuts=record_index::cuts(&h,{},16);
      CHECK(std::is_sorted(cuts.begin(),cuts.end()));
      CHECK(cuts.size()==(records.empty() ? 0 : std::min<size_t>(16,records.size())-1));
    }
  }
  record_index::destroy(&h);
  CHECK(!h.root && pool.stats().live_bytes==0);
  pool.trim(); CHECK(pool.stats().reserved_bytes==0);
}
}
int main() { failures(); model(); std::puts("PASS: mutable slab-wide index, counts/ranges/rank selection, and 20,000 model operations"); }
