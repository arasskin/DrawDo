#pragma once
#include "document_model.h"
#include "task_screen.h"
namespace document {
class screen {
 struct hit{native_ui::rect r;std::function<void()> action;};std::vector<hit> hits;
 std::string status;int64_t status_at=0;
 void button(native_ui::surface& c,native_ui::rect r,const std::string& text,std::function<void()> action){c.shape(r,0x20323fff,false);c.text(text,r.x+8,r.y+10,13,0x70e7baff,r.w-16);hits.push_back({r,std::move(action)});}
public:
 model& data;
 std::function<void(std::string,native_ui::rect,std::function<void(std::string)>)> edit_text;
 explicit screen(model& data):data(data){}
 void draw(native_ui::surface& c){
  hits.clear();c.clip({0,0,c.width,c.height});c.text("Shared document",18,24,26,0xe8f2f7ff,270);
  if(time_us()-status_at>500000){status_at=time_us();auto& s=data.connection.stats();status=!data.online?"Offline · edits stay on this phone":data.connection.connected()?"Connected · signed owner writes":"Connecting to Littlebear";
    status+="   "+std::to_string(s.sent+s.received)+" B";}
  c.text(status,18,67,12,0x70e7baff,364);c.text(data.config.role?"Phone B · independent writer":"Phone A · independent writer",18,88,12,0x87a5b6ff,364);
  button(c,{18,116,112,38},"Type / edit",[this]{auto draft=data.editor();edit_text(data.text(),{18,116,364,100},[this,draft](std::string text) {data.replace(draft,std::move(text));});});
  button(c,{140,116,116,38},"Insert word",[this]{data.replace(data.text()+(data.config.role?" blue":" green"));});
  button(c,{266,116,116,38},data.online?"Go offline":"Reconnect",[this]{data.set_online(!data.online);});
  button(c,{18,164,112,36},"Delete last",[this]{auto chars=scalars(data.text());if(!chars.empty())chars.pop_back();std::string t;for(auto& v:chars)t+=v;data.replace(t);});
  c.text(data.done?"Benchmark complete":data.config.start_ms?"Scripted sync measurement":"Edit either phone; changes merge here",142,174,11,0x87a5b6ff,238);
  c.shape({18,217,364,std::max(90.0f,c.height-300)},0x14232eff,false);
  auto chars=scalars(data.text());size_t max_lines=size_t(std::max(1.0f,(c.height-330)/23));std::string line;size_t column=0,row=0;
  for(size_t i=0;i<chars.size()&&row<max_lines;i++){if(chars[i]=="\n"||column==32){c.text(line,28,230+row*23,16,0xe8f2f7ff,344);line.clear();column=0;++row;if(chars[i]=="\n")continue;}line+=chars[i];++column;}
  if(row<max_lines)c.text(line.empty()&&data.text().empty()?"Your shared document starts here.":line,28,230+row*23,16,0xe8f2f7ff,344);
  c.text(std::to_string(chars.size())+" characters · "+std::to_string(data.edit_count())+" document edits",18,c.height-64,12,0x87a5b6ff,364);
  if(!data.error.empty())c.text(data.error,18,c.height-42,11,0xffbb88ff,364);
  else c.text("App CRDT → Datafriend → Littlebear → peer",18,c.height-42,11,0x70e7baff,364);
 }
 void press(float x,float y){for(auto& h:hits)if(h.r.contains(x,y)){h.action();return;}}
};
}
