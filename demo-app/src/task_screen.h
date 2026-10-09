#pragma once
#include "task_model.h"
#include <cmath>

namespace native_ui {
struct rect {
  float x,y,w,h;
  bool contains(float px,float py)const{return px>=x&&py>=y&&px<x+w&&py<y+h;}
};
struct surface {
  float width,height;
  std::function<void(rect,uint32_t,bool)> shape;
  std::function<void(std::string,float,float,float,uint32_t,float)> text;
  std::function<void(rect)> clip;
};
}
namespace tasks {
// Small immediate drawing surface, with retained query-owned row components.
// Layout and event handlers stay together; C++ scopes own all subscriptions.
class view {
  struct hit {native_ui::rect bounds;std::function<void()> action;};
  std::vector<hit> hits_;
  std::function<void()> pressed_;
  float down_x_=0,down_y_=0,last_y_=0,scroll_=0,max_scroll_=0;
  bool scrolling_=false,reordering_=false,in_list_=false;
  uint32_t dragged_=0;
  native_ui::rect list_{},hit_clip_{0,0,10000,10000};
  void hit(native_ui::rect r,std::function<void()> action) {
    auto right=std::min(r.x+r.w,hit_clip_.x+hit_clip_.w),bottom=std::min(r.y+r.h,hit_clip_.y+hit_clip_.h);
    r.x=std::max(r.x,hit_clip_.x);r.y=std::max(r.y,hit_clip_.y);r.w=right-r.x;r.h=bottom-r.y;
    if(r.w>0&&r.h>0)hits_.push_back({r,std::move(action)});
  }
  static constexpr float row_height=58;
  static constexpr uint32_t ink=0xe8f2f7ff,muted=0x87a5b6ff,mint=0x70e7baff;
  void button(native_ui::surface& c,native_ui::rect r,std::string label,bool selected,std::function<void()> action) {
    c.shape(r,selected?0x28584fff:0x20323fff,false);
    c.text(std::move(label),r.x+10,r.y+9,13,selected?mint:ink,r.w-20);
    hit(r,std::move(action));
  }
  void edit_title(uint32_t id) {
    model.select(id);
    if(!model.detail||!model.detail->exists)return;
    auto it=std::find(model.ids.begin(),model.ids.end(),id);
    if(it!=model.ids.end())scroll_=std::clamp(float(it-model.ids.begin())*row_height,0.0f,max_scroll_);
    float y=list_.y;
    if(it!=model.ids.end())y+=float(it-model.ids.begin())*row_height-scroll_;
    // Keep the editor above the keyboard; detail actions use the composer line.
    if(y>300)y=116;
    edit_text(model.detail->title,{68,y+8,246,42},[this,id](std::string text){ui::attempt(model.error,[&]{model.rename(id,std::move(text));});});
  }
 public:
  screen& model;
  std::function<void(std::string,native_ui::rect,std::function<void(std::string)>)> edit_text;
  explicit view(screen& model):model(model) {}
  void draw(native_ui::surface& c) {
    hits_.clear();hit_clip_={0,0,c.width,c.height};c.clip(hit_clip_);
    c.text("A little more done.",18,24,27,ink,278);
    c.text("Datafriend · stored on this device",19,63,12,muted,350);
    c.text(model.counter,19,87,12,mint,350);
    button(c,{18,116,364,46},"+   What would you like to get done?",false,[this]{
      edit_text("",{18,116,364,46},[this](std::string title){ui::attempt(model.error,[&]{if(model.add(std::move(title)))scroll_=1e9f;});});
    });
    const char* filters[]={"All","Active","Completed"};
    for(int i=0;i<3;i++)button(c,{18.0f+i*124,179,116,36},filters[i],model.filter==filters[i],[this,name=std::string(filters[i])]{
      ui::attempt(model.error,[&]{model.set_filter(name);scroll_=0;});
    });
    bool detail=model.detail&&model.detail->exists;
    list_={18,230,364,std::max(58.0f,c.height-230-(detail?150:48))};
    max_scroll_=std::max(0.0f,float(model.ids.size())*row_height-list_.h);
    scroll_=std::clamp(scroll_,0.0f,max_scroll_);
    if(reordering_){if(last_y_<list_.y+25)scroll_=std::max(0.0f,scroll_-4);if(last_y_>list_.y+list_.h-25)scroll_=std::min(max_scroll_,scroll_+4);}
    size_t first=size_t(scroll_/row_height),visible=size_t(std::ceil(list_.h/row_height))+1;
    model.mount(first,visible);
    c.shape(list_,0x14232eff,false);c.clip(list_);hit_clip_=list_;
    for(size_t index=first;index<std::min(model.ids.size(),first+visible);index++) {
      auto id=model.ids[index];auto& row=*model.rows.at(id);float y=list_.y+index*row_height-scroll_;
      c.shape({18,y,364,row_height-2},model.selected==id?0x1f363eff:0x172a36ff,false);
      button(c,{26,y+10,36,36},row.done?"✓":"",row.done,[this,id]{
        ui::attempt(model.error,[&]{if(auto it=model.rows.find(id);it!=model.rows.end())it->second->toggle();});
      });
      c.text(row.title,72,y+16,16,row.done?muted:ink,234);
      if(row.done)c.shape({72,y+31,224,1},0x87a5b699,false);
      hit({68,y,246,row_height},[this,id]{ui::attempt(model.error,[&]{edit_title(id);});});
      button(c,{315,y+10,30,36},"×",false,[this,id]{ui::attempt(model.error,[&]{model.db.remove(id);});});
      c.text("≡",357,y+13,20,muted,22);
    }
    if(model.ids.empty())c.text("A clear list. Room for something good.",33,list_.y+30,14,muted,332);
    if(reordering_){float y=list_.y+std::floor((last_y_-list_.y+scroll_)/row_height)*row_height-scroll_;
      c.shape({18,y,364,3},mint,false);}
    if(max_scroll_>0){float thumb=std::max(24.0f,list_.h*list_.h/(model.ids.size()*row_height));
      c.shape({379,list_.y+(list_.h-thumb)*scroll_/max_scroll_,3,thumb},0x70e7baaa,false);}
    hit_clip_={0,0,c.width,c.height};c.clip(hit_clip_);
    if(detail){
      auto y=c.height-134;auto id=model.detail->id;
      c.shape({18,y,364,96},0x20323fff,false);
      c.text(model.detail->title,30,y+10,16,ink,340);
      button(c,{26,y+46,114,38},model.detail->done?"Mark active":"Mark done",false,[this]{ui::attempt(model.error,[&]{if(model.detail)model.detail->toggle();});});
      button(c,{148,y+46,65,38},"Edit",false,[this,id]{ui::attempt(model.error,[&]{edit_title(id);});});
      button(c,{221,y+46,75,38},"Delete",false,[this,id]{ui::attempt(model.error,[&]{model.db.remove(id);});});
      button(c,{304,y+46,32,38},"↑",false,[this,id]{ui::attempt(model.error,[&]{auto it=std::find(model.ids.begin(),model.ids.end(),id);if(it!=model.ids.end()&&it!=model.ids.begin())model.move(id,size_t(it-model.ids.begin()-1));});});
      button(c,{344,y+46,32,38},"↓",false,[this,id]{ui::attempt(model.error,[&]{auto it=std::find(model.ids.begin(),model.ids.end(),id);if(it!=model.ids.end())model.move(id,size_t(it-model.ids.begin()+1));});});
    }
    c.text(model.error.empty()?"Tap a title to edit · drag ≡ to reorder":model.error,19,c.height-26,11,model.error.empty()?muted:0xffb4a9ff,363);
  }
  void down(float x,float y) {
    down_x_=x;down_y_=last_y_=y;scrolling_=reordering_=false;dragged_=0;pressed_={};
    in_list_=list_.contains(x,y);
    if(in_list_&&x>=348){auto index=size_t(std::max(0.0f,(y-list_.y+scroll_)/row_height));
      if(index<model.ids.size()){dragged_=model.ids[index];reordering_=true;}return;}
    // List clips also bound hit testing; partially visible rows cannot cover the
    // filters/detail panel. Search the drawn controls from front to back.
    for(auto it=hits_.rbegin();it!=hits_.rend();++it)if(it->bounds.contains(x,y)){
      pressed_=it->action;break;
    }
  }
  void move(float x,float y) {
    if(in_list_&&!reordering_&&(scrolling_||std::abs(y-down_y_)>6||std::abs(x-down_x_)>6)){
      scrolling_=true;pressed_={};scroll_=std::clamp(scroll_+last_y_-y,0.0f,max_scroll_);
    }
    last_y_=y;
  }
  void up(float x,float y,bool cancel=false) {
    auto action=std::move(pressed_);pressed_={};
    if(!cancel&&reordering_&&dragged_){auto dest=size_t(std::max(0.0f,(y-list_.y+scroll_)/row_height));ui::attempt(model.error,[&]{model.move(dragged_,dest);});}
    else if(!cancel&&!scrolling_&&action&&std::abs(y-down_y_)<8&&std::abs(x-down_x_)<8)action();
    reordering_=scrolling_=in_list_=false;dragged_=0;
  }
};
} // namespace tasks
