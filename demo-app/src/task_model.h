#pragma once
#include "datafriend_ui.h"
#include <algorithm>
#include <map>
#include <memory>

namespace tasks {
using A=ui::attribute;
inline std::string trim(std::string text) {
  auto first=text.find_first_not_of(" \t\r\n");
  return first==std::string::npos?"":text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}
// A row owns one narrow query. Actions directly edit Datafriend; the subscription
// refreshes the presentation before the action returns, including other owners.
struct task_row {
  uint32_t id;std::string title;bool done=false,exists=false;uint64_t builds=0;
  ui::store& db;
  ui::subscription view;
  task_row(ui::store& database,uint32_t entity,uint64_t& build_count):id(entity),db(database) {
    view=db.watch(ui::select({
      {{id,A::title,ui::var{0}},true},
      {{id,A::done,ui::var{1}},true},
    }),[this,&build_count](const ui::rows& rows){
      ++builds;++build_count;exists=!rows.empty();
      if(exists){title=rows.text(id,A::title);done=rows.flag(id,A::done);}
    });
  }
  void toggle(){if(exists)db.edit({{id,A::done,!done}});}
  void rename(std::string title){title=trim(std::move(title));if(exists&&!title.empty())db.edit({{id,A::title,title}});}
  void remove(){if(exists)db.remove(id);}
};
class screen {
 public:
  ui::store db; // Outlives every subscription and component below.
 private:
  static constexpr uint32_t settings_id=1;
  size_t first_=0,visible_=0;
  ui::subscription config_,counter_,list_;
  std::optional<ui::rows> ordered_;
  void query_list() {
    list_.reset();
    auto q=ui::select({
      {{ui::var{0},A::kind,"task"},false},
      {{ui::var{0},A::order,ui::var{1}},true},
    });
    if(filter!="All")q.clauses.push_back({{0,{}},ui::name(A::done),{-1,ui::value(filter=="Completed").bytes}});
    list_=db.watch(std::move(q),[this](const ui::rows& rows){
      ++list_builds;ordered_=rows;ids=rows.entities(A::order);mount(first_,visible_);
    });
  }
 public:
  std::string filter,error,counter;
  uint32_t selected=0;
  std::vector<uint32_t> ids;
  std::map<uint32_t,std::unique_ptr<task_row>> rows;
  std::unique_ptr<task_row> detail;
  uint64_t list_builds=0,row_builds=0,counter_builds=0,detail_builds=0;
  explicit screen(const std::string& directory):db(directory) {
    bool initialized=false;
    {auto marker=db.watch(ui::select({{{settings_id,A::appInitialized,true},true}}),
      [&](auto& r){initialized=!r.empty();});}
    if(!initialized) {
      db.edit({{settings_id,A::appInitialized,true},{settings_id,A::taskFilter,"All"},
               {settings_id,A::taskSelected,ui::entity{0}}});
      const char* titles[]={"Make something small","Take a walk outside","Read a chapter",
        "Call a friend","Sketch an idea","Water the plants","Try a new recipe",
        "Tidy the desk","Listen to an album","Plan a little adventure","Write a note","Leave room for play"};
      std::vector<ui::fact> seed;int64_t order=0;
      for(auto title:titles){auto id=db.new_entity();seed.insert(seed.end(),{
        {id,A::kind,"task"},{id,A::title,title},{id,A::done,false},{id,A::order,order++}});}
      db.edit(seed);
    }
    counter_=db.watch(ui::select({
      {{ui::var{0},A::kind,"task"},false},
      {{ui::var{0},A::done,ui::var{1}},true},
    }),[this](const ui::rows& r){
      ++counter_builds;counter=std::to_string(r.count(A::done,true))+" of "+std::to_string(r.entities().size())+" complete";
    });
    config_=db.watch(ui::select({
      {{settings_id,A::taskFilter,ui::var{0}},true},
      {{settings_id,A::taskSelected,ui::var{1}},true},
    }),[this](const ui::rows& r){
      auto next_filter=r.text(settings_id,A::taskFilter);
      if(filter!=next_filter){filter=next_filter;first_=0;query_list();}
      auto next=uint32_t(r.integer(settings_id,A::taskSelected));
      if(selected!=next){selected=next;detail.reset();if(selected)detail=std::make_unique<task_row>(db,selected,detail_builds);}
    });
  }
  void mount(size_t first,size_t count) {
    first_=std::min(first,ids.size());visible_=count;
    auto end=std::min(ids.size(),first_+visible_);
    for(auto it=rows.begin();it!=rows.end();) {
      if(std::find(ids.begin()+first_,ids.begin()+end,it->first)==ids.begin()+end)it=rows.erase(it);else ++it;
    }
    for(size_t i=first_;i<end;i++)if(!rows.contains(ids[i]))rows.emplace(ids[i],std::make_unique<task_row>(db,ids[i],row_builds));
  }
  void set_filter(std::string value) {
    if(value!="All"&&value!="Active"&&value!="Completed")throw std::invalid_argument("Unknown filter");
    db.edit({{settings_id,A::taskFilter,std::move(value)}});
  }
  void select(uint32_t id){db.edit({{settings_id,A::taskSelected,ui::entity{id}}});}
  uint32_t add(std::string title) {
    title=trim(std::move(title));if(title.empty())return 0;
    // Max order comes from a short-lived independent query, including hidden tasks.
    int64_t last=-1;
    {auto order=db.watch(ui::select({{{ui::var{0},A::kind,"task"},false},{{ui::var{0},A::order,ui::var{1}},true}}),
      [&](const auto& r){for(auto id:r.entities())last=std::max(last,r.integer(id,A::order));});}
    auto id=db.new_entity();
    db.edit({{id,A::kind,"task"},{id,A::title,title},{id,A::done,false},{id,A::order,last+1}});
    return id;
  }
  // Editing can outlive a mounted row (scrolling/filtering). Recheck the entity
  // with a scoped query on completion, so an old editor cannot resurrect a task.
  void rename(uint32_t id,std::string title) {
    uint64_t ignored=0;task_row current(db,id,ignored);current.rename(std::move(title));
  }
  void move(uint32_t id,size_t destination) {
    auto it=std::find(ids.begin(),ids.end(),id);if(it==ids.end()||ids.empty())return;
    auto from=size_t(it-ids.begin());destination=std::min(destination,ids.size()-1);
    if(from==destination)return;
    auto reordered=ids;reordered.erase(reordered.begin()+from);reordered.insert(reordered.begin()+destination,id);
    std::vector<ui::fact> changes;
    for(size_t i=std::min(from,destination);i<=std::max(from,destination);i++)
      changes.push_back({reordered[i],A::order,ordered_->integer(ids[i],A::order)});
    db.edit(changes);
  }
};
} // namespace tasks
