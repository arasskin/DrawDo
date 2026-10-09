#pragma once
// Shared native simulation: UI client and matched Dart/C++ benchmark.
#include "datafriend/engine.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <thread>
#ifdef __ANDROID__
#include <sched.h>
#endif
namespace particles {
using namespace datafriend;
using clock_type=std::chrono::steady_clock;
inline int64_t micros(clock_type::time_point start) {
  return std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now()-start).count();
}
constexpr int cols=8, rows=12, cell_count=cols*rows;
constexpr double width=640, height=960, radius=24;
inline int cell_of(double x,double y) { return int(y/80)*cols+int(x/80); }
inline double wrap(double x,double max) { auto r=std::fmod(x,max); return r<0?r+max:r; }
inline bytes scalar(uint8_t tag,uint64_t bits) {
  bytes b(9,'\0');b[0]=char(tag);
  for(int i=0;i<8;i++) b[i+1]=char(bits>>(56-8*i));
  return b;
}
inline bytes real(double v) { return scalar(4,std::bit_cast<uint64_t>(v==0?0.0:v)); }
inline double decode(const bytes* b) {
  if(!b||b->size()!=9||(*b)[0]!=4) throw std::runtime_error("missing/non-double coordinate");
  uint64_t bits=0; for(int i=1;i<=8;i++) bits=(bits<<8)|uint8_t((*b)[i]);
  return std::bit_cast<double>(bits);
}
struct particle { uint32_t id; double x,y; };
struct cell {
  uint64_t id=0;
  query_result result;
  // Insertion order matches Dart's linked map, including stable deletion.
  std::vector<particle> particles;
  ankerl::unordered_dense::map<uint32_t,size_t> index;
  std::vector<float> points;
  bool active=false;
  void reindex() { index.clear(); for(size_t i=0;i<particles.size();i++) index[particles[i].id]=i; }
  int refresh(uint32_t xa,uint32_t ya,bool initial=false) {
    const auto& changes=result.changes();
    auto changed=changes.entities;
    if(initial||changes.reset) { particles.clear();index.clear(); }
    if(initial||changes.membership||changes.reset) {
      auto ids=result.entities();
      ankerl::unordered_dense::set<uint32_t> present(ids.begin(),ids.end());
      std::erase_if(particles,[&](const auto& p){return !present.contains(p.id);});
      reindex();
      for(auto id:ids) if(!index.contains(id)) changed.insert(id);
    }
    int reads=0;
    for(auto id:changed) {
      const auto* x=result.value(id,xa);
      if(!x) {
        if(index.contains(id)) {std::erase_if(particles,[&](auto& p){return p.id==id;});reindex();}
      } else {
        particle p{id,decode(x),decode(result.value(id,ya))};
        if(auto it=index.find(id);it!=index.end()) particles[it->second]=p;
        else {index[id]=particles.size();particles.push_back(p);}
        reads++;
      }
    }
    points.resize(particles.size()*2);
    for(size_t i=0;i<particles.size();i++){points[2*i]=float(particles[i].x);points[2*i+1]=float(particles[i].y);}
    return reads;
  }
};
struct sample {
  int64_t physics,prepare,edit,readback,total;
  int moved,crossings,changed,reads;
  uint64_t pairs,interactions;
};
class simulation {
 public:
  engine db;
  std::vector<cell> cells;
  std::vector<std::pair<double,double>> drift;
  int count,tick=0,patch_col=3,patch_row=5;
  bool local;
  bool automatic_patch=true;
  uint32_t xa,ya;
  // App bindings run after publication, on the calling thread. They only read
  // results/update derived caches; they must not submit recursive edits.
  std::function<void(const std::vector<uint64_t>&)> on_changed;
  int refresh(const std::vector<uint64_t>& ids) {
    int reads=0;
    for(auto id:ids) {
      auto it=std::find_if(cells.begin(),cells.end(),[&](auto& c){return c.id==id;});
      if(it!=cells.end())reads+=it->refresh(xa,ya);
    }
    if(on_changed)on_changed(ids);
    return reads;
  }
  void modify(edit changes) { refresh(db.modify(std::move(changes))); }
  simulation(const char* directory,const double* initial,int n,bool local):db(directory),count(n),local(local) {
    edit seed;drift.resize(n+1);
    for(int i=0;i<n;i++) {
      auto id=uint32_t(i+1);auto x=initial[4*i],y=initial[4*i+1];
      seed.add.push_back({id,"x",real(x)});seed.add.push_back({id,"y",real(y)});
      seed.add.push_back({id,"region",scalar(3,cell_of(x,y))});
      drift[id]={initial[4*i+2],initial[4*i+3]};
    }
    db.modify(std::move(seed));cells.reserve(cell_count);
    for(int i=0;i<cell_count;i++) {
      query q; q.clauses={{{0,{}},"region",{-1,scalar(3,i)}},{{0,{}},"x",{1,{}}},{{0,{}},"y",{2,{}}}};
      q.projection={1,2};cell c;
      auto [id,result]=db.subscribe(std::move(q));c.id=id;c.result=std::move(result);
      xa=c.result.scope()->find("x").value();ya=c.result.scope()->find("y").value();
      c.refresh(xa,ya,true);cells.push_back(std::move(c));
    }
  }
  void update_active() {
    for(int i=0;i<cell_count;i++) {
      auto dx=std::abs(i%cols-patch_col),dy=std::abs(i/cols-patch_row);
      cells[i].active=!local||(std::min(dx,cols-dx)<=1&&std::min(dy,rows-dy)<=1);
    }
  }
  sample step(edit changes = {}) {
    const auto start=clock_type::now();
    if(local&&automatic_patch&&tick>0&&tick%90==0){patch_col=(patch_col+1)%cols;if(patch_col==0)patch_row=(patch_row+1)%rows;}
    update_active();
    std::vector<particle> updates;
    uint64_t pairs=0,interactions=0;int crossings=0;
    for(int i=0;i<cell_count;i++) {
      const auto& c=cells[i];if(!c.active)continue;
      std::vector<particle> neighbors;
      for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++) {
        auto& other=cells[((i/cols+dy+rows)%rows)*cols+(i%cols+dx+cols)%cols];
        neighbors.insert(neighbors.end(),other.particles.begin(),other.particles.end());
      }
      for(const auto& p:c.particles) {
        double fx=0,fy=0;
        for(const auto& other:neighbors) {
          if(p.id==other.id)continue;pairs++;
          auto dx=p.x-other.x,dy=p.y-other.y;
          if(dx>width/2)dx-=width;if(dx<-width/2)dx+=width;
          if(dy>height/2)dy-=height;if(dy<-height/2)dy+=height;
          auto squared=dx*dx+dy*dy;
          if(squared>=radius*radius||squared<1e-12)continue;
          interactions++;auto distance=std::sqrt(squared);auto force=(1-distance/radius)*80/distance;
          fx+=dx*force;fy+=dy*force;
        }
        auto x=wrap(p.x+std::clamp((drift[p.id].first+fx)/60,-10.0,10.0),width);
        auto y=wrap(p.y+std::clamp((drift[p.id].second+fy)/60,-10.0,10.0),height);
        updates.push_back({p.id,x,y});if(cell_of(x,y)!=i)crossings++;
      }
    }
    auto physics=micros(start);
    for(const auto& p:updates) {
      changes.add.push_back({p.id,"x",real(p.x)});changes.add.push_back({p.id,"y",real(p.y)});
      auto next=cell_of(p.x,p.y);
      if(!cells[next].index.contains(p.id))changes.add.push_back({p.id,"region",scalar(3,next)});
    }
    auto prepared=micros(start);
    auto ids=db.modify(std::move(changes));
    // Native client dispatch: map changed handles to the same cached cell owners.
    std::vector<cell*> dirty;
    for(auto id:ids) {
      auto it=std::find_if(cells.begin(),cells.end(),[&](auto& c){return c.id==id;});
      if(it!=cells.end())dirty.push_back(&*it);
    }
    auto edited=micros(start);int reads=0;
    for(auto* c:dirty)reads+=c->refresh(xa,ya);
    if(on_changed)on_changed(ids);
    tick++;auto total=micros(start);
    return {physics,prepared-physics,edited-prepared,total-edited,total,int(updates.size()),crossings,int(dirty.size()),reads,pairs,interactions};
  }
  std::vector<particle> validate(uint64_t non_particle_facts=0) {
    query q;q.all=true;auto [id,r]=db.subscribe(q);
    auto ra=r.scope()->find("region").value();std::vector<particle> all;
    for(int i=0;i<cell_count;i++) {
      const auto& c=cells[i];if(c.result.size()!=2*c.particles.size())throw std::runtime_error("cell size mismatch");
      for(auto p:c.particles) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||cell_of(p.x,p.y)!=i||
           decode(r.value(p.id,xa))!=p.x||decode(r.value(p.id,ya))!=p.y||
           !r.value(p.id,ra)||*r.value(p.id,ra)!=scalar(3,i))throw std::runtime_error("cell value mismatch");
        all.push_back(p);
      }
    }
    if(int(all.size())!=count||r.size()!=uint64_t(count)*3+non_particle_facts)throw std::runtime_error("particle count mismatch");
    std::sort(all.begin(),all.end(),[](auto& a,auto& b){return a.id<b.id;});
    for(int i=0;i<count;i++)if(all[i].id!=uint32_t(i+1))throw std::runtime_error("duplicate entity");
    db.unsubscribe(id);return all;
  }
};
}
