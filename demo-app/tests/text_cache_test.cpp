#include "render/text_cache.h"
#include <iostream>
#include <memory>
#include <set>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct texture {
  int id;std::set<int>& live;
  texture(int id,std::set<int>& live):id(id),live(live){live.insert(id);}
  ~texture(){live.erase(id);}
};
int main(){
  try{
    std::set<int> live;int builds=0;
    auto build=[&]{return std::make_unique<texture>(++builds,live);};
    labels::texture_cache<std::unique_ptr<texture>> cache(100);
    auto get=[&](std::string text,size_t bytes,float pixels=16,int width=100){
      return cache.insert(text,pixels,width,bytes,build).get();
    };
    auto a=get("A",20),b=get("B",30),c=get("C",50);
    auto a_id=a->id,c_id=c->id;
    check(cache.bytes()==100&&cache.size()==3,"exact budget fit");
    check(get("A",20)==a&&builds==3,"sharing does not double charge memory");
    cache.end_frame();
    check(get("C",50)==c&&get("A",20)==a&&get("B",30)==b&&builds==3,"reorder reuses textures");
    cache.end_frame();get("B",30);auto d=get("D",60);
    check(cache.bytes()==90&&cache.size()==2,"mixed sizes evict enough old textures");
    check(!live.contains(a_id)&&!live.contains(c_id)&&live.contains(b->id),"LRU destruction releases unpinned resources");
    bool full=false;try{get("E",11);}catch(const std::runtime_error&){full=true;}
    check(full&&builds==4&&live.contains(d->id)&&cache.bytes()==90,"budget pressure preserves current draws and avoids upload");
    get("E",10);check(cache.bytes()==100,"remaining bytes can be used exactly");
    cache.end_frame();
    bool large=false;try{get("oversized",101);}catch(const std::runtime_error&){large=true;}
    check(large&&cache.bytes()==100&&cache.size()==3,"oversized raster rejected before eviction/upload");
    bool failed=false;
    try{cache.insert("failed",16,100,100,[]{throw std::runtime_error("upload failed");return std::unique_ptr<texture>{};});}
    catch(const std::runtime_error&){failed=true;}
    check(failed&&!cache.find("failed",16,100)&&cache.bytes()==0&&live.empty(),"failed upload leaves accounting and ownership coherent");
    get("failed",20);check(cache.bytes()==20,"failed upload can be retried");
    cache.clear();check(cache.bytes()==0&&cache.size()==0&&live.empty(),"clear releases GPU resources and accounting");
    {
      labels::texture_cache<std::unique_ptr<texture>> large_cache(10000);
      auto first=large_cache.insert("first",16,100,4,build).get();
      for(int i=0;i<1000;i++)large_cache.insert("label "+std::to_string(i),16,100,4,build);
      check(large_cache.size()==1001&&large_cache.bytes()==4004,"cache grows past 96 entries according to bytes");
      check(large_cache.find("first",16,100)->get()==first,"draw pointers and borrowed keys survive hash growth");
      auto previous=builds;
      large_cache.insert("first",17,100,4,build);large_cache.insert("first",16,90,4,build);
      check(builds==previous+2,"font and clipping width distinguish rasters");
      large_cache.end_frame();check(large_cache.find("first",16,100)->get()==first,"next frame retains cached textures");
    }
    check(live.empty(),"cache destruction releases its resources");
    {
      labels::texture_cache<std::unique_ptr<texture>> gpu_limited(1000,2);
      gpu_limited.insert("A",16,100,1,build);gpu_limited.insert("B",16,100,1,build);
      gpu_limited.end_frame();gpu_limited.find("A",16,100);
      gpu_limited.insert("C",16,100,1,build);
      check(!gpu_limited.find("B",16,100)&&gpu_limited.bytes()==2,"backend handle pressure also evicts safely");
    }
    check(live.empty(),"no leaked textures");
    std::cout<<"PASS: byte budget, LRU eviction, pinning, >96 entries, stable draws, raster keys and failure recovery\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
