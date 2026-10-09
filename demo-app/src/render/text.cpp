// Shared GPU text cache and batched textured quads, adapted from junstodos.
#include "render/text.h"
#include "render/text_cache.h"
#include "profiling/metrics.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "render/shaders.h"
#include "platform/text_raster.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <memory>
namespace labels {
namespace {
struct vertex {float x,y,u,v;uint32_t color;};
constexpr size_t draw_capacity=96;
struct label {
  int width=0,height=0;sg_image image{};sg_view view{};
  ~label(){if(view.id)sg_destroy_view(view);if(image.id)sg_destroy_image(image);}
};
texture_cache<std::unique_ptr<label>> cache(texture_budget_bytes,texture_handle_limit);
struct prepared {const label* texture=nullptr;float scale=1;};
std::array<prepared,draw_capacity> slots;
std::vector<vertex> vertices;
struct draw_call {const label* texture;float x,y,w,h;};
std::vector<draw_call> draws;
float clip_x=0,clip_y=0,clip_w=10000,clip_h=10000;
sg_sampler sampler{};sg_buffer buffer{};sg_pipeline pipeline{};
uint32_t packed(uint32_t rgba){auto a=rgba&255;return ((((rgba>>24)&255)*a+127)/255)|(((((rgba>>16)&255)*a+127)/255)<<8)|(((((rgba>>8)&255)*a+127)/255)<<16)|(a<<24);}
}
void init(){
  text_raster::init();
  sg_sampler_desc sm{};sm.min_filter=SG_FILTER_LINEAR;sm.mag_filter=SG_FILTER_LINEAR;sm.wrap_u=sm.wrap_v=SG_WRAP_CLAMP_TO_EDGE;sampler=sg_make_sampler(&sm);
  sg_buffer_desc bd{};bd.size=sizeof(vertex)*6*draw_capacity;bd.usage.vertex_buffer=true;bd.usage.stream_update=true;buffer=sg_make_buffer(&bd);
  sg_pipeline_desc pd{};pd.shader=sg_make_shader(text_shader_desc(sg_query_backend()));
  pd.layout.attrs[ATTR_text_pos].format=SG_VERTEXFORMAT_FLOAT2;pd.layout.attrs[ATTR_text_texcoord0].format=SG_VERTEXFORMAT_FLOAT2;pd.layout.attrs[ATTR_text_color0].format=SG_VERTEXFORMAT_UBYTE4N;
  auto& blend=pd.colors[0].blend;blend.enabled=true;blend.src_factor_rgb=blend.src_factor_alpha=SG_BLENDFACTOR_ONE;blend.dst_factor_rgb=blend.dst_factor_alpha=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  pipeline=sg_make_pipeline(&pd);
}
void prepare(int slot,const std::string& text,float size,float scale,float max_width){
  task_profile::timer total(task_profile::current.text);
  if(task_profile::enabled)++task_profile::current.labels;
  auto& output=slots.at(slot);auto pixels=size*scale;
  auto width=int(max_width*scale);
  auto texture=cache.find(text,pixels,width);
  if(!texture){
    if(task_profile::enabled)++task_profile::current.text_misses;
    auto raster_start=task_profile::enabled?task_profile::now():0;
    auto bitmap=text_raster::render(text,pixels,width);
    auto& copy=bitmap.pixels;
    if(task_profile::enabled)task_profile::current.raster+=task_profile::now()-raster_start;
    task_profile::timer uploading(task_profile::current.upload);
    auto& inserted=cache.insert(text,pixels,width,copy.size(),[&]{
    auto owned=std::make_unique<label>();auto& l=*owned;
    l.width=bitmap.width;l.height=bitmap.height;
    sg_image_desc id{};id.width=l.width;id.height=l.height;id.pixel_format=SG_PIXELFORMAT_RGBA8;id.data.mip_levels[0]={copy.data(),copy.size()};l.image=sg_make_image(&id);
    if(sg_query_image_state(l.image)!=SG_RESOURCESTATE_VALID)throw std::runtime_error("Text image creation failed");
    sg_view_desc vd{};vd.texture.image=l.image;l.view=sg_make_view(&vd);
    if(sg_query_view_state(l.view)!=SG_RESOURCESTATE_VALID)throw std::runtime_error("Text image view creation failed");
    return owned;
    });
    texture=&inserted;
  }
  output={texture->get(),scale};
  if(task_profile::enabled){task_profile::current.texture_bytes=cache.bytes();task_profile::current.texture_entries=cache.size();}
}
void clip(float x,float y,float w,float h){clip_x=x;clip_y=y;clip_w=w;clip_h=h;}
void draw(int slot,float x,float y,uint32_t rgba){
  auto prepared=slots.at(slot);
  if(!prepared.texture)throw std::runtime_error("Text drawing slot was not prepared");
  auto& l=*prepared.texture;if(!l.image.id)return;
  if(draws.size()>=draw_capacity)throw std::runtime_error("Text batch capacity exceeded");
  auto right=x+l.width/prepared.scale,bottom=y+l.height/prepared.scale;auto c=packed(rgba);
  const vertex v[]={{x,y,0,0,c},{right,y,1,0,c},{right,bottom,1,1,c},{x,y,0,0,c},{right,bottom,1,1,c},{x,bottom,0,1,c}};
  vertices.insert(vertices.end(),std::begin(v),std::end(v));draws.push_back({prepared.texture,clip_x,clip_y,clip_w,clip_h});
}
void flush(float width,float height){
  if(draws.empty()){cache.end_frame();slots={};return;}
  sg_range data{vertices.data(),vertices.size()*sizeof(vertex)};
  {task_profile::timer t(task_profile::current.label_upload);sg_update_buffer(buffer,&data);}
  task_profile::timer drawing(task_profile::current.label_submit);sg_apply_pipeline(pipeline);
  text_vs_params_t params{};params.fb_size[0]=width;params.fb_size[1]=height;sg_apply_uniforms(UB_text_vs_params,SG_RANGE(params));
  for(size_t i=0;i<draws.size();i++){
    auto d=draws[i];float scale=sapp_widthf()/width;
    sg_apply_scissor_rect(int(d.x*scale),int(d.y*scale),int(d.w*scale),int(d.h*scale),true);
    sg_bindings bind{};bind.vertex_buffers[0]=buffer;bind.views[VIEW_tex]=d.texture->view;bind.samplers[SMP_smp]=sampler;sg_apply_bindings(&bind);sg_draw(int(i)*6,6,1);
  }
  vertices.clear();draws.clear();cache.end_frame();slots={};
}
void shutdown(){
  cache.clear();slots={};
  sg_destroy_pipeline(pipeline);sg_destroy_buffer(buffer);sg_destroy_sampler(sampler);
  text_raster::shutdown();
}
}
