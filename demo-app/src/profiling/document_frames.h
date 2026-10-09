#pragma once
#include "profiling/task_benchmark.h"
#include <sstream>
#include <chrono>
namespace document {
class frame_probe {
 struct sample {int64_t start=0,interval=0,cpu=0,total=0,work=0,present=-1,steady_offset=0;uint64_t id=0,peer_tick=0;
#ifdef __APPLE__
 std::shared_ptr<std::atomic<int64_t>> displayed;
#endif
 } current;
 std::vector<sample> samples;int64_t cpu_start=0,previous=0;bool active=false;
#ifdef __ANDROID__
 EGLDisplay display;EGLSurface surface;PFNEGLGETNEXTFRAMEIDANDROIDPROC next=nullptr;PFNEGLGETFRAMETIMESTAMPSANDROIDPROC timestamps=nullptr;
#endif
public:
 void init(){
#ifdef __ANDROID__
 display=eglGetCurrentDisplay();surface=eglGetCurrentSurface(EGL_DRAW);
 next=(PFNEGLGETNEXTFRAMEIDANDROIDPROC)eglGetProcAddress("eglGetNextFrameIdANDROID");timestamps=(PFNEGLGETFRAMETIMESTAMPSANDROIDPROC)eglGetProcAddress("eglGetFrameTimestampsANDROID");
 if(!eglSurfaceAttrib(display,surface,EGL_TIMESTAMPS_ANDROID,EGL_TRUE))next=nullptr;
#endif
 }
 void collect(){for(size_t i=samples.size()>20?samples.size()-20:0;i<samples.size();i++){auto& s=samples[i];if(s.present>0)continue;
#ifdef __ANDROID__
 if(timestamps&&s.id){EGLint field=EGL_DISPLAY_PRESENT_TIME_ANDROID;EGLnsecsANDROID value=-1;if(timestamps(display,surface,s.id,1,&field,&value))s.present=value;}
#else
 if(s.displayed)s.present=s.displayed->load(std::memory_order_relaxed);
#endif
 }}
 void begin(bool record){collect();active=record;if(!active)return;current={};current.present=-1;current.start=task_profile::now();current.interval=previous?current.start-previous:0;previous=current.start;cpu_start=task_profile::cpu();
 current.steady_offset=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()-task_profile::now();
#ifdef __ANDROID__
 if(next)next(display,surface,&current.id);
#endif
 }
 void work_done(uint64_t peer_tick){if(active){current.work=task_profile::now()-current.start;current.peer_tick=peer_tick;}}
 void track(const void* raw){
#ifdef __APPLE__
 if(!active||!raw)return;auto d=(__bridge id<MTLDrawable>)raw;auto stamp=std::make_shared<std::atomic<int64_t>>(-1);current.displayed=stamp;current.id=d.drawableID;
 auto offset=task_profile::now()-int64_t(CACurrentMediaTime()*1e9);
 [d addPresentedHandler:^(id<MTLDrawable> v){if(v.presentedTime>0)stamp->store(int64_t(v.presentedTime*1e9)+offset,std::memory_order_relaxed);}];
#else
 (void)raw;
#endif
 }
 void end(){if(!active)return;current.cpu=task_profile::cpu()-cpu_start;current.total=task_profile::now()-current.start;samples.push_back(current);}
 std::string json(){collect();std::ostringstream f;f<<"\"frames\":[";bool first=true;for(auto s:samples){if(!first)f<<',';first=false;f<<'['<<s.start<<','<<s.interval<<','<<s.cpu<<','<<s.total<<','<<s.work<<','<<s.present<<','<<s.peer_tick<<','<<s.steady_offset<<']';}f<<']';return f.str();}
};
}
