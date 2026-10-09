#pragma once
#include "profiling/metrics.h"
#include "task_screen.h"
#include "platform/runtime.h"
#ifdef __ANDROID__
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/native_activity.h>
#endif
#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <atomic>
#include <memory>
#endif
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cstdlib>

namespace task_profile {
struct row {
  int frame=0,action=0;int64_t start=0,interval=0,action_ns=0,prepare_ns=0,submit_ns=0,total_ns=0,cpu_ns=0;
#ifdef __APPLE__
  std::shared_ptr<std::atomic<int64_t>> presented;
#endif
  metrics phases;uint64_t frame_id=0;int64_t queue=-1,gpu_done=-1,latch=-1,present=-1;
  uint64_t list_builds=0,row_builds=0,counter_builds=0,detail_builds=0;
};
class session {
  std::vector<row> rows_;
#ifdef __ANDROID__
  EGLDisplay display_=EGL_NO_DISPLAY;EGLSurface surface_=EGL_NO_SURFACE;
  PFNEGLGETNEXTFRAMEIDANDROIDPROC next_=nullptr;
  PFNEGLGETFRAMETIMESTAMPSANDROIDPROC timestamps_=nullptr;
#endif
  bool presentation_=false;int frame_=-1;
  int64_t previous_=0,cpu_start_=0;
  std::string output_;uint32_t first_=0,added_=0;
  std::function<void(std::string)> submit_;
  uint64_t list_start_=0,row_start_=0,counter_start_=0,detail_start_=0;
  void presentation_init(){
#ifdef __ANDROID__
    display_=eglGetCurrentDisplay();surface_=eglGetCurrentSurface(EGL_DRAW);
    auto ext=eglQueryString(display_,EGL_EXTENSIONS);
    if(!ext||!strstr(ext,"EGL_ANDROID_get_frame_timestamps"))return;
    next_=(PFNEGLGETNEXTFRAMEIDANDROIDPROC)eglGetProcAddress("eglGetNextFrameIdANDROID");
    timestamps_=(PFNEGLGETFRAMETIMESTAMPSANDROIDPROC)eglGetProcAddress("eglGetFrameTimestampsANDROID");
    auto supports=(PFNEGLGETFRAMETIMESTAMPSUPPORTEDANDROIDPROC)eglGetProcAddress("eglGetFrameTimestampSupportedANDROID");
    presentation_=next_&&timestamps_&&supports&&supports(display_,surface_,EGL_DISPLAY_PRESENT_TIME_ANDROID)
      &&supports(display_,surface_,EGL_RENDERING_COMPLETE_TIME_ANDROID)&&supports(display_,surface_,EGL_COMPOSITION_LATCH_TIME_ANDROID)
      &&supports(display_,surface_,EGL_REQUESTED_PRESENT_TIME_ANDROID)&&eglSurfaceAttrib(display_,surface_,EGL_TIMESTAMPS_ANDROID,EGL_TRUE);
    platform::log(false,"TaskProfile","EGL presentation timestamps: %d",presentation_);

#else
    presentation_=true;
#endif
  }
  void collect(){
#ifdef __ANDROID__
    if(!presentation_)return;
    const EGLint fields[]={EGL_REQUESTED_PRESENT_TIME_ANDROID,EGL_RENDERING_COMPLETE_TIME_ANDROID,EGL_COMPOSITION_LATCH_TIME_ANDROID,EGL_DISPLAY_PRESENT_TIME_ANDROID};
    // Query a small recent window. Fence timestamps arrive asynchronously.
    for(int i=std::max(0,int(rows_.size())-12);i<int(rows_.size())-1;i++){
      auto& r=rows_[i];if(!r.frame_id||r.present>0)continue;
      EGLnsecsANDROID out[4];if(timestamps_(display_,surface_,r.frame_id,4,fields,out)){
        r.queue=out[0];r.gpu_done=out[1];r.latch=out[2];r.present=out[3];
      }
    }

#else
    for(int i=std::max(0,int(rows_.size())-12);i<int(rows_.size());i++){
      auto& r=rows_[i];if(r.presented&&r.present<0)r.present=r.presented->load(std::memory_order_relaxed);
    }
#endif
  }
  void finish(tasks::screen& model){
    if(rows_.size()!=size_t(warmup+frames+12))throw std::runtime_error("Profile frame coverage");
    if(!model.error.empty())throw std::runtime_error(model.error);
    if(kind==1&&model.list_builds!=list_start_)throw std::runtime_error("Toggle rebuilt All list");
    if(kind==2&&(model.list_builds!=list_start_||model.counter_builds!=counter_start_))throw std::runtime_error("Rename rebuilt list/counter");
    if(kind==4&&(model.row_builds!=row_start_||model.counter_builds!=counter_start_))throw std::runtime_error("Reorder rebuilt row/counter");
    auto [id,result]=model.db.engine.subscribe(ui::select({{{ui::var{0},tasks::A::kind,"task"},true},{{ui::var{0},tasks::A::title,ui::var{1}},true},{{ui::var{0},tasks::A::done,ui::var{2}},true},{{ui::var{0},tasks::A::order,ui::var{3}},true}}));
    ui::rows all(std::move(result));
    if(all.entities().size()!=size_t(count+(added_?1:0)))throw std::runtime_error("Profile task count");
    model.db.engine.unsubscribe(id);
    std::ofstream f(output_+".tmp");
    f<<"{\"count\":"<<count<<",\"case\":"<<kind<<",\"warmup\":"<<warmup<<",\"frames\":"<<frames<<",\"egl_timestamps\":"<<
#ifdef __ANDROID__
      presentation_
#else
      false
#endif
      <<",\"presentation_timestamps\":"<<presentation_<<",\"checks\":\"passed\",\"rows\":[";
    bool first=true;
    for(auto& r:rows_)if(r.frame>=warmup&&r.frame<warmup+frames){
      if(!first)f<<',';first=false;
      f<<"{\"frame\":"<<r.frame<<",\"action\":"<<r.action<<",\"start_ns\":"<<r.start<<",\"interval_ns\":"<<r.interval
       <<",\"action_ns\":"<<r.action_ns<<",\"prepare_ns\":"<<r.prepare_ns<<",\"submit_ns\":"<<r.submit_ns<<",\"total_ns\":"<<r.total_ns<<",\"cpu_ns\":"<<r.cpu_ns;
#define FIELD(name) f<<",\"" #name "_ns\":"<<r.phases.name
      FIELD(encode);FIELD(engine);FIELD(publish);FIELD(subscribe);FIELD(unsubscribe);FIELD(text);FIELD(raster);FIELD(upload);FIELD(geometry_upload);FIELD(shape_submit);FIELD(label_upload);FIELD(label_submit);FIELD(commit);FIELD(diagnostics);FIELD(jni_attach);FIELD(jni_detach);FIELD(java_raster);FIELD(bitmap_copy);FIELD(pass_begin);FIELD(shape_draw);
#undef FIELD
      f<<",\"edits\":"<<r.phases.edits<<",\"subscriptions\":"<<r.phases.subscriptions<<",\"unsubscriptions\":"<<r.phases.unsubscriptions
       <<",\"text_misses\":"<<r.phases.text_misses<<",\"texture_bytes\":"<<r.phases.texture_bytes<<",\"texture_entries\":"<<r.phases.texture_entries<<",\"jni_attachments\":"<<r.phases.jni_attachments<<",\"labels\":"<<r.phases.labels<<",\"frame_id\":"<<r.frame_id
       <<",\"queue_ns\":"<<r.queue<<",\"gpu_done_ns\":"<<r.gpu_done<<",\"latch_ns\":"<<r.latch<<",\"present_ns\":"<<r.present
       <<",\"list_builds\":"<<r.list_builds<<",\"row_builds\":"<<r.row_builds<<",\"counter_builds\":"<<r.counter_builds<<",\"detail_builds\":"<<r.detail_builds<<'}';
    }
    f<<"]";
#ifdef __APPLE__
    auto token=std::getenv("DATAFRIEND_PROFILE_ID");
    if(token&&strspn(token,"0123456789abcdef")==strlen(token))f<<",\"run_id\":\""<<token<<"\"";
    f<<",\"thermal_state\":"<<int(NSProcessInfo.processInfo.thermalState)<<",\"low_power\":"<<bool(NSProcessInfo.processInfo.lowPowerModeEnabled);
#endif
    f<<"}\n";f.close();if(!f)throw std::runtime_error("Profile output failed");
    std::filesystem::rename(output_+".tmp",output_);done=true;enabled=false;
    platform::log(false,"TaskProfile","Complete: count=%d case=%d frames=%d",count,kind,frames);
  }
 public:
  bool active=false,done=false;int count=12,kind=0,warmup=60,frames=360;
  row current_row;
  void configure(const std::string& directory){
    active=platform::profile_options(count,kind,warmup,frames);
    if(!active)return;
    if(count<12||count>10000||kind<0||kind>7||warmup<10||frames<30||frames>10000)throw std::runtime_error("Invalid profile config");
    output_=directory+"/task-profile.json";
    std::filesystem::remove(output_);rows_.reserve(warmup+frames+12);presentation_init();
  }
  void track_drawable(const void* raw){
#ifdef __APPLE__
    if(!active||done||!raw)return;
    auto drawable=(__bridge id<MTLDrawable>)raw;
    auto time=std::make_shared<std::atomic<int64_t>>(-1);
    current_row.presented=time;current_row.frame_id=drawable.drawableID;
    const auto offset=now()-int64_t(CACurrentMediaTime()*1e9);
    [drawable addPresentedHandler:^(id<MTLDrawable> displayed){
      if(displayed.presentedTime>0)time->store(int64_t(displayed.presentedTime*1e9)+offset,std::memory_order_relaxed);
    }];
#else
    (void)raw;
#endif
  }
  void seed(tasks::screen& model,tasks::view& view){
    if(!active)return;
    std::vector<ui::fact> seed;
    for(int i=int(model.ids.size());i<count;i++){auto id=model.db.new_entity();seed.insert(seed.end(),{{id,tasks::A::kind,"task"},{id,tasks::A::title,"Profile task "+std::to_string(i)},{id,tasks::A::done,false},{id,tasks::A::order,int64_t(i)}});}
    if(!seed.empty())model.db.edit(seed);
    seed.clear();for(size_t i=0;i<model.ids.size();i++)seed.push_back({model.ids[i],tasks::A::done,bool(i%2)});
    model.db.edit(seed);first_=model.ids.front();model.select(first_);
    view.edit_text=[this](std::string,native_ui::rect,std::function<void(std::string)> submit){submit_=std::move(submit);};
  }
  bool begin(tasks::screen& model,tasks::view& view){
    if(!active||done)return false;
    collect();
    if(frame_+1>=warmup+frames+12){finish(model);return false;}
    ++frame_;current={};enabled=true;
    current_row={};current_row.frame=frame_;current_row.start=now();current_row.interval=previous_?current_row.start-previous_:0;previous_=current_row.start;cpu_start_=cpu();
#ifdef __ANDROID__
    if(presentation_)next_(display_,surface_,&current_row.frame_id);
#endif
    if(frame_==warmup){list_start_=model.list_builds;row_start_=model.row_builds;counter_start_=model.counter_builds;detail_start_=model.detail_builds;}
    auto start=now();
    bool action=frame_>=10&&frame_<warmup+frames&&frame_%6==0;
    auto tap=[&](float x,float y){view.down(x,y);view.up(x,y);};
    if(action){
      switch(kind){
        case 1:tap(44,259);break;
        case 2:model.rename(first_,frame_%12?"A title edited by the UI":"A title changed once more");break;
        case 3:tap(frame_%12?280:50,197);break;
        case 4:view.down(360,259);view.move(360,317);view.up(360,317);break;
        case 6:
          if(added_){model.db.remove(added_);added_=0;}
          else {added_=model.add("A new task from the editor");}
          break;
      }
      if(kind!=0&&kind!=5)current_row.action=1;
    }
    if((kind==5||kind==7)&&frame_>=10&&frame_<warmup+frames){
      if(frame_==10)view.down(100,500);
      auto phase=(frame_-10)%240;auto y=phase<120?500.0f-phase*3:140.0f+(phase-120)*3;
      if(kind==7)y=500.0f-(frame_-10)*12;
      view.move(100,y);current_row.action=1;
    }
    current_row.action_ns=now()-start;
    return true;
  }
  void end(tasks::screen& model,int64_t prepare,int64_t submit){
    current_row.prepare_ns=prepare;current_row.submit_ns=submit;current_row.total_ns=now()-current_row.start;current_row.cpu_ns=cpu()-cpu_start_;
    current_row.phases=current;current_row.list_builds=model.list_builds;current_row.row_builds=model.row_builds;current_row.counter_builds=model.counter_builds;current_row.detail_builds=model.detail_builds;
    rows_.push_back(current_row);
  }
};
} // namespace task_profile
