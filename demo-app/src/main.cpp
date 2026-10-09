// Sokol lifecycle adapted from recovered junstodos/src/main.cpp.
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_log.h"
#include "render/shaders.h"
#include "render/text.h"
#include "particle_model.h"
#include "task_screen.h"
#include "platform/text_input.h"
#include "profiling/task_benchmark.h"
#include "platform/runtime.h"
#include "document_screen.h"
#include "profiling/document_frames.h"
#include <filesystem>

extern "C" void demo_trace_frame();
namespace {
constexpr float ui_width=400;
constexpr uint32_t ink=0xe8f2f7ff,muted=0x87a5b6ff,mint=0x70e7baff;
using rect=native_ui::rect;
struct vertex {float x,y,u,v;uint32_t color;float circle;};
std::unique_ptr<demo::app> app;
std::unique_ptr<tasks::screen> task_model;
std::unique_ptr<tasks::view> task_view;
bool tasks_mode=true,document_mode=false;
std::unique_ptr<datafriend::identity> document_identity;
std::unique_ptr<document::model> document_model;
std::unique_ptr<document::screen> document_view;
document::frame_probe document_probe;
int document_drain=0;bool document_saved=false;
task_profile::session profiler;
rect draw_clip{0,0,10000,10000};
std::vector<vertex> geometry;
sg_buffer buffer{};sg_pipeline pipeline{};sg_pass_action pass_action{};
float ui_height=800,scale=1,ui_top=0;rect board{};
bool dragging=false,failed=false;
double fps=60;int frames=0,display_frames=0;double elapsed=0,last_display=0;auto previous_frame=particles::clock_type::now();
std::string metrics="Starting native simulation",workload="";
std::string directory;
uint32_t packed(uint32_t c){return (c>>24)|((c>>8)&0xff00)|((c<<8)&0xff0000)|((c&255)<<24);}
void quad(rect r,uint32_t color,bool circle=false){
  auto original=r;
  auto right=std::min(r.x+r.w,draw_clip.x+draw_clip.w),bottom=std::min(r.y+r.h,draw_clip.y+draw_clip.h);
  r.x=std::max(r.x,draw_clip.x);r.y=std::max(r.y,draw_clip.y);r.w=right-r.x;r.h=bottom-r.y;
  if(r.w<=0||r.h<=0)return;
  float u0=(r.x-original.x)/original.w*2-1,v0=(r.y-original.y)/original.h*2-1;
  float u1=(right-original.x)/original.w*2-1,v1=(bottom-original.y)/original.h*2-1;
  r.y+=ui_top;
  auto c=packed(color);auto k=circle?1.0f:0.0f;
  vertex v[]={{r.x,r.y,u0,v0,c,k},{r.x+r.w,r.y,u1,v0,c,k},{r.x+r.w,r.y+r.h,u1,v1,c,k},{r.x,r.y,u0,v0,c,k},{r.x+r.w,r.y+r.h,u1,v1,c,k},{r.x,r.y+r.h,u0,v1,c,k}};
  geometry.insert(geometry.end(),std::begin(v),std::end(v));
}
// Fixed layout is presentation; labels/selection/actions come from query bindings.
rect button_bounds(size_t i) {
  if(i<4)return {18.0f+float(i)*93,112,85,35};
  return {18.0f+float(i-4)*127,161,i==6?110.0f:119.0f,34};
}
void layout(){
  scale=sapp_widthf()/ui_width;
  auto insets=platform::safe_area();
  ui_top=insets.top/scale;
  ui_height=sapp_heightf()/scale-ui_top-insets.bottom/scale;
  float h=std::min(ui_height-302.0f,364.0f*1.5f);h=std::max(h,80.0f);
  board={(ui_width-h/1.5f)/2,211,h/1.5f,h};
}
void label(int slot,float x,float y,uint32_t color){labels::draw(slot,x,y+ui_top,color);}
void prepare_labels(){
  labels::prepare(0,"Cell particles",28,scale);labels::prepare(1,"C++  /  Sokol  /  Datafriend",12,scale);
  for(size_t i=0;i<app->buttons.size();i++)labels::prepare(int(2+i),app->buttons[i].label,i<4?14:13,scale);
  labels::prepare(9,metrics,14,scale);labels::prepare(10,workload,12,scale);
  labels::prepare(11,app->state.status,12,scale);
  labels::prepare(12,"PARTICLES",10,scale);
}
void init(){
  try{
    sg_desc desc{};desc.image_pool_size=desc.view_pool_size=labels::texture_handle_pool_size;desc.environment=sglue_environment();desc.logger.func=slog_func;sg_setup(&desc);
    pass_action.colors[0].load_action=SG_LOADACTION_CLEAR;pass_action.colors[0].clear_value={16/255.0f,25/255.0f,35/255.0f,1};
    sg_buffer_desc bd{};bd.size=sizeof(vertex)*50000;bd.usage.vertex_buffer=true;bd.usage.stream_update=true;buffer=sg_make_buffer(&bd);
    sg_pipeline_desc pd{};pd.shader=sg_make_shader(shape_shader_desc(sg_query_backend()));
    pd.layout.attrs[ATTR_shape_pos].format=SG_VERTEXFORMAT_FLOAT2;pd.layout.attrs[ATTR_shape_uv0].format=SG_VERTEXFORMAT_FLOAT2;
    pd.layout.attrs[ATTR_shape_color0].format=SG_VERTEXFORMAT_UBYTE4N;pd.layout.attrs[ATTR_shape_circle0].format=SG_VERTEXFORMAT_FLOAT;
    auto& b=pd.colors[0].blend;b.enabled=true;b.src_factor_rgb=b.src_factor_alpha=SG_BLENDFACTOR_ONE;b.dst_factor_rgb=b.dst_factor_alpha=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pipeline=sg_make_pipeline(&pd);labels::init();
    auto root=platform::data_directory();
    directory=root+"/particle-session";
    std::filesystem::remove_all(directory);std::filesystem::create_directories(directory);
    profiler.configure(root);
    auto task_directory=root+(profiler.active?"/tasks-profile":"/tasks-v1");
    if(profiler.active)std::filesystem::remove_all(task_directory);
    task_model=std::make_unique<tasks::screen>(task_directory);
    task_view=std::make_unique<tasks::view>(*task_model);
    task_view->edit_text=[](std::string text,rect r,std::function<void(std::string)> submit){
      editor::open(std::move(text),r.x*scale,(r.y+ui_top)*scale,r.w*scale,r.h*scale,std::move(submit));
    };
    profiler.seed(*task_model,*task_view);
    document_identity=std::make_unique<datafriend::identity>("document-demo-v1",false);
    std::ofstream(root+"/document-public-key.txt")<<datafriend::public_key_hex(*document_identity)<<'\n';
    document::configuration cfg;
    if(!profiler.active&&cfg.load(root)){
      document_model=std::make_unique<document::model>(root,cfg,*document_identity);
      document_view=std::make_unique<document::screen>(*document_model);
      document_view->edit_text=[](std::string text,rect r,std::function<void(std::string)> changed){
        editor::open_live(std::move(text),r.x*scale,(r.y+ui_top)*scale,r.w*scale,r.h*scale,std::move(changed));
      };
      document_probe.init();document_mode=true;tasks_mode=false;platform::keep_awake(true);
    }
    layout();
    platform::log(false,"DrawDoSokol","Ready: tasks with scoped Datafriend row views");
  }catch(const std::exception& e){failed=true;platform::log(true,"DrawDoSokol","Initialization failed: %s",e.what());}
}
void present(){
    sg_range data{geometry.data(),geometry.size()*sizeof(vertex)};{task_profile::timer t(task_profile::current.geometry_upload);sg_update_buffer(buffer,&data);}
    {task_profile::timer t(task_profile::current.shape_submit);
    sg_pass pass{};pass.action=pass_action;pass.swapchain=sglue_swapchain();profiler.track_drawable(pass.swapchain.metal.current_drawable);if(document_mode)document_probe.track(pass.swapchain.metal.current_drawable);{task_profile::timer t(task_profile::current.pass_begin);sg_begin_pass(&pass);}
    task_profile::timer drawing(task_profile::current.shape_draw);
    sg_apply_scissor_rect(0,0,sapp_width(),sapp_height(),true);sg_apply_pipeline(pipeline);sg_bindings bind{};bind.vertex_buffers[0]=buffer;sg_apply_bindings(&bind);
    shape_params_t params{};params.fb_size[0]=ui_width;params.fb_size[1]=sapp_heightf()/scale;sg_apply_uniforms(UB_shape_params,SG_RANGE(params));sg_draw(0,int(geometry.size()),1);}
    labels::flush(ui_width,sapp_heightf()/scale);
    {task_profile::timer t(task_profile::current.commit);sg_end_pass();demo_trace_frame();sg_commit();}
}
void task_frame(){
  bool measuring=profiler.begin(*task_model,*task_view);
  auto prepare_start=measuring?task_profile::now():0;
  editor::poll();layout();geometry.clear();int slot=0;
  native_ui::surface surface{ui_width,ui_height,
    [](rect r,uint32_t color,bool circle){quad(r,color,circle);},
    [&](std::string text,float x,float y,float size,uint32_t color,float width){
      labels::prepare(slot,text,size,scale,width);label(slot++,x,y,color);
    },
    [](rect r){draw_clip=r;labels::clip(r.x,r.y+ui_top,r.w,r.h);}};
  task_view->draw(surface);
  quad({302,22,80,30},0x20323fff);labels::prepare(90,"Document",11,scale);label(90,308,30,mint);
  auto submit_start=measuring?task_profile::now():0;
  present();
  auto submit_end=measuring?task_profile::now():0;
  static uint64_t revision=UINT64_MAX;uint64_t current;
  {task_profile::timer t(task_profile::current.diagnostics);current=task_model->db.engine.revision();}
  if(measuring)profiler.end(*task_model,submit_start-prepare_start,submit_end-submit_start);
  if(profiler.active)return;
  if(revision!=current){revision=current;platform::log(false,"DrawDoTasks","revision=%llu list=%llu rows=%llu counter=%llu detail=%llu mounted=%zu filter=%s items=%zu",(unsigned long long)current,(unsigned long long)task_model->list_builds,(unsigned long long)task_model->row_builds,(unsigned long long)task_model->counter_builds,(unsigned long long)task_model->detail_builds,task_model->rows.size(),task_model->filter.c_str(),task_model->ids.size());}
}
void document_frame(){
  // Capture before either phone starts editing: their wall clocks can differ,
  // even though one-way timing uses separately calibrated monotonic clocks.
  bool recording=document_model&&document_model->config.start_ms&&document::wall_ms()>=document_model->config.start_ms-5000&&!document_model->done;
  document_probe.begin(recording);
  editor::poll();if(document_model)document_model->pump();
  document_probe.work_done(document_model?document_model->remote_tick():0);layout();geometry.clear();draw_clip={0,0,ui_width,ui_height};labels::clip(0,0,ui_width,sapp_heightf()/scale);int slot=0;
  native_ui::surface surface{ui_width,ui_height,
    [](rect r,uint32_t color,bool circle){quad(r,color,circle);},
    [&](std::string text,float x,float y,float size,uint32_t color,float width){labels::prepare(slot,text,size,scale,width);label(slot++,x,y,color);},
    [](rect r){draw_clip=r;labels::clip(r.x,r.y+ui_top,r.w,r.h);}};
  if(document_view)document_view->draw(surface);
  else {surface.text("Shared document",18,25,25,ink,280);surface.text("Pair both phones with tools/sync_phones.py",18,110,14,muted,364);}
  quad({302,22,80,30},0x20323fff);labels::prepare(90,"Particles",11,scale);label(90,311,30,mint);
  present();document_probe.end();
  if(document_model)document_model->calibration.pump();
  if(document_model&&document_model->done&&!document_saved&&++document_drain>=15){document_model->save(document_probe.json());document_saved=true;platform::log(false,"DocumentSync","Complete: edits=%llu",static_cast<unsigned long long>(document_model->edit_count()));}
}
void frame(){
  if(failed)return;
  try{
    if(document_mode){document_frame();return;}
    if(document_model)document_model->pump();
    if(tasks_mode){task_frame();return;}
    layout();draw_clip={0,0,ui_width,ui_height};labels::clip(0,0,ui_width,sapp_heightf()/scale);
    auto& simulation=app->simulation;const auto& state=app->state;
    const auto count=state.count;const auto paused=state.paused,local=state.local;
    auto now=particles::clock_type::now();double dt=std::chrono::duration<double>(now-previous_frame).count();previous_frame=now;elapsed+=dt;frames++;display_frames++;
    app->step();const auto& last=app->last;
    if(elapsed-last_display>.5||frames==1){
      if(elapsed>last_display){fps=display_frames/(elapsed-last_display);display_frames=0;}
      char line[160];if(paused)snprintf(line,sizeof(line),"Paused    %.2f ms last step",last.total/1000.0);
      else snprintf(line,sizeof(line),"%.0f fps    %.2f ms step",fps,last.total/1000.0);metrics=line;
      if(paused)snprintf(line,sizeof(line),"%d particles   /   96 cell views",count);
      else snprintf(line,sizeof(line),"%d moving   /   %d cells updated",last.moved,last.changed);workload=line;
      last_display=elapsed;
      if(frames%120<35)platform::log(false,"DrawDoSokol","frame=%d count=%d mode=%s paused=%d fps=%.1f step_us=%lld moved=%d cells=%d button_builds=%llu",frames,count,local?"local":"all",paused,fps,(long long)last.total,last.moved,last.changed,(unsigned long long)app->button_builds());
    }
    prepare_labels();geometry.clear();
    for(size_t i=0;i<app->buttons.size();i++)quad(button_bounds(i),app->buttons[i].selected?0x28584fff:0x20323fff);
    quad(board,0x14232eff);
    auto cw=board.w/particles::cols,ch=board.h/particles::rows;
    for(int i=0;i<particles::cell_count;i++)if(simulation.cells[i].active)quad({board.x+(i%particles::cols)*cw,board.y+(i/particles::cols)*ch,cw,ch},0x193c3bff);
    for(int x=0;x<=particles::cols;x++)quad({board.x+x*cw,board.y,.45f,board.h},0x29424cff);
    for(int y=0;y<=particles::rows;y++)quad({board.x,board.y+y*ch,board.w,.45f},0x29424cff);
    for(const auto& c:simulation.cells)for(auto p:c.particles){
      auto x=board.x+float(p.x/particles::width)*board.w,y=board.y+float(p.y/particles::height)*board.h;
      quad({x-1.8f,y-1.8f,3.6f,3.6f},c.active?0x8ef5caff:0x84a7bfff,true);
    }
    label(0,18,23,ink);label(1,19,62,muted);label(12,19,92,muted);
    for(size_t i=0;i<app->buttons.size();i++) {
      auto r=button_bounds(i);label(int(2+i),r.x+(i<4?20:i==6?29:15),r.y+(i<4?7:8),app->buttons[i].selected?mint:ink);
    }
    label(9,18,board.y+board.h+16,ink);label(10,18,board.y+board.h+39,muted);label(11,18,board.y+board.h+61,mint);
    quad({302,22,80,30},0x20323fff);labels::prepare(90,"Tasks",12,scale);label(90,320,29,mint);
    present();
  }catch(const std::exception& e){failed=true;platform::log(true,"DrawDoSokol","Frame failed: %s",e.what());}
}
void patch(float x,float y){
  app->move_patch(int((x-board.x)/board.w*particles::cols),int((y-board.y)/board.h*particles::rows));
}
void event(const sapp_event* e){
  if(failed||!task_model)return;
  if(profiler.active)return;
  try {
    if(e->type==SAPP_EVENTTYPE_SUSPENDED){editor::poll();editor::cancel();task_view->up(0,0,true);if(app)app->validate();platform::log(false,"DrawDoSokol","Suspend: task views alive; particle database validated if active");return;}
    if(e->type==SAPP_EVENTTYPE_RESUMED){previous_frame=particles::clock_type::now();elapsed=last_display=0;display_frames=0;return;}
    if(!e->num_touches)return;
    auto x=e->touches[0].pos_x/scale,y=e->touches[0].pos_y/scale-ui_top;
    if(e->type==SAPP_EVENTTYPE_TOUCHES_BEGAN&&rect{302,22,80,30}.contains(x,y)) {
      editor::cancel();
      if(tasks_mode){tasks_mode=false;document_mode=true;}
      else if(document_mode){document_mode=false;if(!app)app=std::make_unique<demo::app>(directory.c_str());}
      else tasks_mode=true;
      return;
    }
    if(document_mode){if(document_view&&e->type==SAPP_EVENTTYPE_TOUCHES_BEGAN)document_view->press(x,y);return;}
    if(tasks_mode){
      if(e->type==SAPP_EVENTTYPE_TOUCHES_BEGAN)task_view->down(x,y);
      if(e->type==SAPP_EVENTTYPE_TOUCHES_MOVED)task_view->move(x,y);
      if(e->type==SAPP_EVENTTYPE_TOUCHES_ENDED||e->type==SAPP_EVENTTYPE_TOUCHES_CANCELLED)task_view->up(x,y,e->type==SAPP_EVENTTYPE_TOUCHES_CANCELLED);
      return;
    }
    if(e->type==SAPP_EVENTTYPE_TOUCHES_ENDED||e->type==SAPP_EVENTTYPE_TOUCHES_CANCELLED){dragging=false;return;}
    if(e->type==SAPP_EVENTTYPE_TOUCHES_BEGAN){
      for(size_t i=0;i<app->buttons.size();i++)if(button_bounds(i).contains(x,y)) {
        auto before=app->button_builds();auto start=particles::clock_type::now();
        app->buttons[i].press();
        platform::log(false,"DrawDoSokol","Action: button=%d edit_us=%lld rebuilt_buttons=%llu count=%d paused=%d local=%d",int(i),(long long)particles::micros(start),(unsigned long long)(app->button_builds()-before),app->state.count,app->state.paused,app->state.local);
        return;
      }
      dragging=app->state.local&&board.contains(x,y);if(dragging)patch(x,y);
    }else if(e->type==SAPP_EVENTTYPE_TOUCHES_MOVED&&dragging)patch(x,y);
  }catch(const std::exception& ex){failed=true;platform::log(true,"DrawDoSokol","Action failed: %s",ex.what());}
}
void cleanup(){editor::cancel();document_view.reset();document_model.reset();document_identity.reset();task_view.reset();task_model.reset();app.reset();labels::shutdown();sg_destroy_pipeline(pipeline);sg_destroy_buffer(buffer);sg_shutdown();}
}
sapp_desc sokol_main(int,char**){sapp_desc d{};d.init_cb=init;d.frame_cb=frame;d.event_cb=event;d.cleanup_cb=cleanup;d.high_dpi=true;d.window_title="Datafriend demo";d.logger.func=slog_func;return d;}
