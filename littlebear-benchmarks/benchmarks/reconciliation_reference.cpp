// Optional comparison only: the pinned original is fetched into build/.
#define RECONCILIATION_BENCHMARK_LIBRARY
#include "reconciliation.cpp"
#include "negentropy.h"

namespace reference {
struct index : negentropy::StorageBase {
  std::vector<negentropy::Item> items;
  std::vector<negentropy::Accumulator> prefix;
  explicit index(const benchmark::index &source) {
    negentropy::Accumulator acc;
    acc.setToZero(); prefix.push_back(acc);
    for (auto id : source.ids) {
      negentropy::Item item(0);
      for (size_t i = 0; i < 8; ++i) item.id[i] = uint8_t(id >> (56 - 8 * i));
      items.push_back(item);
      acc.add(item); prefix.push_back(acc);
    }
  }
  uint64_t size() override { return items.size(); }
  const negentropy::Item &getItem(size_t i) override { return items.at(i); }
  void iterate(size_t low, size_t high, std::function<bool(const negentropy::Item &,size_t)> cb) override {
    for (size_t i = low; i < high; ++i) if (!cb(items[i],i)) break;
  }
  size_t findLowerBound(size_t low, size_t high, const negentropy::Bound &bound) override {
    return std::lower_bound(items.begin()+low,items.begin()+high,bound.item)-items.begin();
  }
  negentropy::Fingerprint fingerprint(size_t low, size_t high) override {
    auto acc = prefix[high]; acc.sub(prefix[low]); return acc.getFingerprint(high-low);
  }
};
void scenario(const char *name, size_t shift, bool replacement, unsigned empty_side) {
  benchmark::index a,b;
  constexpr size_t N=16384;
  for (size_t i=0;i<N;++i) { a.ids.push_back(benchmark::mix(i)); if(empty_side != 1)b.ids.push_back(benchmark::mix(i+shift)); }
  if(replacement)b.ids.back()=benchmark::mix(1000000);
  benchmark::seal(&a);benchmark::seal(&b);
  if(empty_side == 2) { a.ids.clear(); a.prefix.assign(1,0); }
  index left(a),right(b);
  std::array<double,9> times{};
  size_t total_allocations=0, bytes=0, messages=0, have_count=0, need_count=0;
  for(auto &elapsed:times) {
    Negentropy client(left,0),server(right,0);
    allocations=0;bytes=messages=have_count=need_count=0;
    counting=true;
    auto started=std::chrono::steady_clock::now();
    auto query=client.initiate();
    for(;;) {
      bytes+=query.size(); ++messages;
      auto response=server.reconcile(query);
      bytes+=response.size(); ++messages;
      std::vector<std::string> have,need;
      auto next=client.reconcile(response,have,need);
      have_count+=have.size(); need_count+=need.size();
      if(!next)break;
      query=std::move(*next);
      if(messages>100)std::abort();
    }
    elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count();
    counting=false;total_allocations=allocations;
    if(have_count!=(empty_side==1?N:empty_side==2?0:replacement?1:shift)||need_count!=(empty_side==1?0:empty_side==2?N:replacement?1:shift))std::abort();
  }
  std::sort(times.begin(),times.end());
  std::printf("original %s: median_us=%.2f messages=%zu wire_bytes=%zu have=%zu need=%zu cpp_allocations=%zu\n",name,times[4],messages,bytes,have_count,need_count,total_allocations);
}
}
int main() {
  std::printf("Original ID=%zu fingerprint=%zu Item=%zu Accumulator=%zu\n",negentropy::ID_SIZE,negentropy::FINGERPRINT_SIZE,sizeof(negentropy::Item),sizeof(negentropy::Accumulator));
  reference::scenario("equal",0,false,false);
  reference::scenario("one replacement",0,true,false);
  reference::scenario("half overlap",8192,false,false);
  reference::scenario("disjoint",16384,false,false);
  reference::scenario("empty responder",0,false,1);
  reference::scenario("empty initiator",0,false,2);
}
