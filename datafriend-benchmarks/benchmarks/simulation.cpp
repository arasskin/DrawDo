#include "datafriend/engine.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <sys/resource.h>
using namespace datafriend;
using clock_type=std::chrono::steady_clock;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
term var(int v) { return {v,{}}; }
term constant(bytes b) { return {-1,std::move(b)}; }
long peak_rss_kib() {
  rusage usage{}; if(getrusage(RUSAGE_SELF,&usage)) throw std::runtime_error("getrusage");
#ifdef __APPLE__
  return usage.ru_maxrss/1024;
#else
  return usage.ru_maxrss;
#endif
}
int main(int argc,char** argv) {
  try {
    if(argc!=8) throw std::invalid_argument("directory views entities_per_view percent stable|churn fixed|group samples");
    std::string path=argv[1]; size_t views=std::stoul(argv[2]), width=std::stoul(argv[3]);
    size_t percent=std::stoul(argv[4]), samples=std::stoul(argv[7]);
    bool churn=std::string(argv[5])=="churn",fixed=std::string(argv[6])=="fixed";
    check(views&&width&&samples&&percent&&percent<=100&&(!fixed||(width==1&&!churn)),"invalid shape");
    std::filesystem::create_directories(path);
    engine db(path,durability::asynchronous);
    edit seed; std::vector<uint32_t> ids(views*width);
    for(size_t v=0;v<views;++v) for(size_t e=0;e<width;++e) {
      auto id=uint32_t(v*width+e+1); ids[v*width+e]=id;
      seed.add.insert(seed.add.end(),{{id,"owner",entity_value(v+1)},{id,"x","0"},{id,"y","0"}});
    }
    db.modify(std::move(seed));
    const auto rss_before=peak_rss_kib();
    const auto subscription_start=clock_type::now();
    std::vector<uint64_t> handles; std::vector<query_result> results;
    for(size_t v=0;v<views;++v) {
      query q;
      if(fixed) q={{{constant(entity_value(ids[v])),"x",var(0)},{constant(entity_value(ids[v])),"y",var(1)}},{0,1}};
      else q={{{var(0),"owner",constant(entity_value(v+1))},{var(0),"x",var(1)},{var(0),"y",var(2)}},{1,2}};
      auto handle=db.subscribe(q); handles.push_back(handle); results.push_back(db.result(handle));
      check(results.back().size()==2*width,"initial view");
    }
    const auto subscription_us=std::chrono::duration<double,std::micro>(clock_type::now()-subscription_start).count();
    const auto rss_subscribed=peak_rss_kib();
    const size_t changed=std::max(size_t(1),views*percent/100);
    uint32_t next=uint32_t(views*width+1);
    std::cout<<"views,entities_per_view,percent,membership,routing,sample,edit_us,recomputed,notified,subscription_us,rss_before_kib,rss_subscribed_kib,rss_current_peak_kib";
#ifdef DATAFRIEND_PROFILE
    std::cout<<",stage_us,indexes_us,queries_us,commit_us";
    std::cout<<",routing_old_us,index_update_us,routing_new_us";
    std::cout<<",routing_us,routing_maintenance_us";
#ifdef DATAFRIEND_PROFILE_INDEXES
    std::cout<<",index_lookup_us,index_ave_remove_us,index_aev_remove_us,index_box_us,index_aev_insert_us,index_ave_insert_us";
#endif
#endif
    std::cout<<'\n';
    for(size_t tick=0;tick<samples+5;++tick) {
      edit update;
      const std::string before=std::to_string(tick),after=std::to_string(tick+1);
      for(size_t v=0;v<changed;++v) {
        auto& id=ids[v*width];
        if(churn) {
          update.remove.insert(update.remove.end(),{{id,"owner",entity_value(v+1)},{id,"x",before},{id,"y","0"}});
          id=next++;
          update.add.insert(update.add.end(),{{id,"owner",entity_value(v+1)},{id,"x",after},{id,"y","0"}});
        } else update.add.push_back({id,"x",after});
      }
      auto start=clock_type::now(); auto notifications=db.modify(std::move(update));
      auto elapsed=std::chrono::duration<double,std::micro>(clock_type::now()-start).count();
      check(notifications.size()==changed,"notification fan-out incorrect");
      for(size_t v=0;v<views;++v) {
        check(results[v].size()==2*width,"view membership incorrect");
        auto item=results[v].lookup(ids[v*width],"x");
        check(item&&item->value->value==(v<changed?after:"0"),"live value incorrect");
      }
      if(tick>=5) {
        std::cout<<views<<','<<width<<','<<percent<<','<<(churn?"churn":"stable")<<','<<(fixed?"fixed":"group")<<','<<tick-5<<','<<elapsed<<','<<db.last_recomputed()<<','<<notifications.size();
        std::cout<<','<<subscription_us<<','<<rss_before<<','<<rss_subscribed<<','<<peak_rss_kib();
#ifdef DATAFRIEND_PROFILE
        auto profile=db.last_profile();
        std::cout<<','<<profile.stage_ns/1000.0<<','<<profile.indexes_ns/1000.0<<','<<profile.queries_ns/1000.0<<','<<profile.commit_ns/1000.0;
        std::cout<<','<<profile.routing_old_ns/1000.0<<','<<profile.index_update_ns/1000.0<<','<<profile.routing_new_ns/1000.0;
        std::cout<<','<<(profile.routing_lookup_ns+profile.routing_old_ns+profile.routing_new_ns)/1000.0
                 <<','<<profile.routing_maintenance_ns/1000.0;
#ifdef DATAFRIEND_PROFILE_INDEXES
        std::cout<<','<<profile.index_lookup_ns/1000.0<<','<<profile.index_ave_remove_ns/1000.0<<','<<profile.index_aev_remove_ns/1000.0
                 <<','<<profile.index_box_ns/1000.0<<','<<profile.index_aev_insert_ns/1000.0<<','<<profile.index_ave_insert_ns/1000.0;
#endif
#endif
        std::cout<<'\n';
      }
    }
    db.flush();
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
