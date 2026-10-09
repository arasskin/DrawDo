#pragma once
#include <cstdint>
#include <time.h>
namespace task_profile {
inline bool enabled=false;
inline int64_t now(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return int64_t(t.tv_sec)*1000000000+t.tv_nsec;}
inline int64_t cpu(){timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return int64_t(t.tv_sec)*1000000000+t.tv_nsec;}
struct metrics {
  uint64_t texture_bytes=0,texture_entries=0;
  int64_t encode=0,engine=0,publish=0,subscribe=0,unsubscribe=0,text=0,raster=0,upload=0;
  int64_t geometry_upload=0,shape_submit=0,label_upload=0,label_submit=0,commit=0,diagnostics=0;
  int64_t pass_begin=0,shape_draw=0;
  int64_t jni_attach=0,jni_detach=0,java_raster=0,bitmap_copy=0;
  int jni_attachments=0;
  int edits=0,subscriptions=0,unsubscriptions=0,text_misses=0,labels=0;
};
inline metrics current;
struct timer {
  int64_t* total;int64_t start;
  explicit timer(int64_t& total):total(enabled?&total:nullptr),start(enabled?now():0){}
  ~timer(){if(total)*total+=now()-start;}
};
} // namespace task_profile
