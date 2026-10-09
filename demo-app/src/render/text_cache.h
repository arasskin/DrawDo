#pragma once
#include <cstdint>
#include <limits>
#include <list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace labels {
// Texture bytes are charged before upload. Current-frame entries form a pinned
// MRU prefix; only older entries may be evicted. Payload destruction releases GPU
// resources. Hash keys borrow strings owned by stable list nodes.
template<class Texture>
class texture_cache {
  struct key {
    std::string_view text;float pixels;int width;
    bool operator==(const key&)const=default;
  };
  struct hash {
    size_t operator()(const key& k)const noexcept {
      auto h=std::hash<std::string_view>{}(k.text);
      h^=std::hash<float>{}(k.pixels)+0x9e3779b9+(h<<6)+(h>>2);
      return h^(std::hash<int>{}(k.width)+0x9e3779b9+(h<<6)+(h>>2));
    }
  };
  struct entry {
    std::string text;float pixels;int width;size_t bytes;uint64_t frame;
    Texture texture;
    key identity()const{return {text,pixels,width};}
  };
  std::list<entry> entries_;
  std::unordered_map<key,typename std::list<entry>::iterator,hash> index_;
  size_t budget_,resource_limit_,bytes_=0,pinned_bytes_=0,pinned_count_=0;
  uint64_t frame_=1;
 public:
  explicit texture_cache(size_t budget,size_t resource_limit=std::numeric_limits<size_t>::max())
    :budget_(budget),resource_limit_(resource_limit){}
  texture_cache(const texture_cache&)=delete;
  texture_cache& operator=(const texture_cache&)=delete;
  const Texture* find(std::string_view text,float pixels,int width) {
    auto found=index_.find({text,pixels,width});if(found==index_.end())return nullptr;
    auto it=found->second;
    if(it->frame!=frame_){it->frame=frame_;pinned_bytes_+=it->bytes;++pinned_count_;}
    entries_.splice(entries_.begin(),entries_,it);
    return &it->texture;
  }
  template<class Build>
  const Texture& insert(std::string_view text,float pixels,int width,size_t bytes,Build&& build) {
    if(auto hit=find(text,pixels,width))return *hit;
    if(bytes>budget_-pinned_bytes_||pinned_count_>=resource_limit_)
      throw std::runtime_error("Text textures needed by one frame exceed the cache budget or GPU handle capacity");
    while(bytes_>budget_-bytes||entries_.size()>=resource_limit_) {
      auto& old=entries_.back();
      index_.erase(old.identity());bytes_-=old.bytes;entries_.pop_back();
    }
    entries_.push_front({std::string(text),pixels,width,bytes,frame_,build()});
    try{index_.emplace(entries_.front().identity(),entries_.begin());}
    catch(...){entries_.pop_front();throw;}
    bytes_+=bytes;pinned_bytes_+=bytes;++pinned_count_;
    return entries_.front().texture;
  }
  void end_frame(){pinned_bytes_=0;pinned_count_=0;++frame_;}
  void clear(){index_.clear();entries_.clear();bytes_=pinned_bytes_=pinned_count_=0;++frame_;}
  size_t bytes()const{return bytes_;}
  size_t size()const{return entries_.size();}
  size_t budget()const{return budget_;}
};
} // namespace labels
