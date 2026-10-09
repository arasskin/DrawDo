#pragma once
#include "particle_simulation.h"
#include "datafriend_ui.h"
#include <array>
#include <random>

namespace demo {
using namespace datafriend;
// Reserved above the demo's maximum particle ID (3000).
constexpr uint32_t controls_entity=10000;
constexpr std::array<int,4> counts={100,300,1000,3000};
inline bytes integer(int n) { return particles::scalar(3,uint64_t(n)); }
inline bytes boolean(bool b) { return bytes{char(1),char(b)}; }
inline uint64_t number(const query_result& r,std::string_view name) {
  auto fact=r.lookup(controls_entity,name);
  if(!fact)throw std::runtime_error("Missing simulation setting");
  auto& b=fact->value->value;
  if(b.size()==2&&b[0]==1)return uint8_t(b[1]);
  if(b.size()!=9||b[0]!=3)throw std::runtime_error("Invalid simulation setting");
  uint64_t n=0;for(int i=1;i<9;i++)n=(n<<8)|uint8_t(b[i]);return n;
}
inline query settings_query(std::initializer_list<const char*> attributes) {
  query q;int v=0;
  for(auto name:attributes) {
    q.clauses.push_back({{-1,entity_value(controls_entity)},name,{v++,{}}});
    q.projection.push_back(uint32_t(q.clauses.size()-1));
  }
  return q;
}
inline std::vector<double> initial_particles(int n) {
  std::mt19937_64 rng(74291);
  std::uniform_real_distribution<double> x(0,particles::width),y(0,particles::height);
  std::vector<double> initial(n*4);
  for(int i=0;i<n;i++) {
    auto angle=(i+1)*2.399963229728653;
    initial[i*4]=x(rng);initial[i*4+1]=y(rng);
    initial[i*4+2]=std::cos(angle)*45;initial[i*4+3]=std::sin(angle)*45;
  }
  return initial;
}
struct button {
  std::string label;
  bool selected=false;
  uint64_t builds=0;
  std::function<void()> press;
};
// This is a derived UI cache, rebuilt from the live settings query. Every action
// writes facts, never these fields. Render resources/timing remain outside DB.
struct settings {
  int count=0,column=0,row=0;
  bool paused=false,local=false,automatic=false;
  std::string status;
  uint64_t builds=0;
};
class app {
  // Temporary constructor seed, discarded after the simulation is initialized.
  explicit app(const char* directory,const std::vector<double>& initial)
      :simulation(directory,initial.data(),1000,true),views(simulation.db) {
    simulation.automatic_patch=false; // the app advances the stored patch instead
    edit seed;seed.add={
      {controls_entity,"simulation/count",integer(1000)},
      {controls_entity,"simulation/paused",boolean(false)},
      {controls_entity,"simulation/local",boolean(true)},
      {controls_entity,"simulation/automatic",boolean(true)},
      {controls_entity,"simulation/patch-column",integer(3)},
      {controls_entity,"simulation/patch-row",integer(5)}};
    simulation.modify(std::move(seed));
    views.watch(settings_query({"simulation/count","simulation/paused","simulation/local",
        "simulation/automatic","simulation/patch-column","simulation/patch-row"}),
      [this](const auto& r){
        state.count=int(number(r,"simulation/count"));state.paused=number(r,"simulation/paused");
        state.local=number(r,"simulation/local");state.automatic=number(r,"simulation/automatic");
        state.column=int(number(r,"simulation/patch-column"));state.row=int(number(r,"simulation/patch-row"));
        state.status=state.paused?"Paused - tap Resume to continue":
          state.local?"Drag the active patch":"Every particle moves each tick";
        ++state.builds;
        simulation.local=state.local;simulation.patch_col=state.column;simulation.patch_row=state.row;
        simulation.update_active();
      });
    for(size_t i=0;i<counts.size();i++) {
      buttons[i].label=std::to_string(counts[i]);
      buttons[i].press=[this,i]{set_count(counts[i]);};
      views.watch(settings_query({"simulation/count"}),[this,i](const auto& r){
        auto& b=buttons[i];b.selected=number(r,"simulation/count")==uint64_t(counts[i]);++b.builds;
      });
    }
    for(size_t i=4;i<6;i++) {
      buttons[i].label=i==4?"Local patch":"All moving";
      buttons[i].press=[this,i]{set("simulation/local",boolean(i==4));};
      views.watch(settings_query({"simulation/local"}),[this,i](const auto& r){
        auto& b=buttons[i];b.selected=bool(number(r,"simulation/local"))==(i==4);++b.builds;
      });
    }
    buttons[6].press=[this]{set("simulation/paused",boolean(!state.paused));};
    views.watch(settings_query({"simulation/paused"}),[this](const auto& r){
      auto& b=buttons[6];b.selected=number(r,"simulation/paused");
      b.label=b.selected?"Resume":"Pause";++b.builds;
    });
    simulation.on_changed=[this](const auto& ids){views.publish(ids);};
  }
  void set(const char* attribute,bytes value) {
    edit e;e.add.push_back({controls_entity,attribute,std::move(value)});
    simulation.modify(std::move(e));
  }
 public:
  particles::simulation simulation;
  ui::view_bindings views;
  settings state;
  std::array<button,7> buttons;
  particles::sample last{};
  explicit app(const char* directory):app(directory,initial_particles(1000)) {}
  app(const app&)=delete;
  app& operator=(const app&)=delete;
  ~app() { simulation.on_changed={}; }
  // Count + particle replacement is one transaction. Engine, views and settings
  // survive; no pending action or database recreation is involved.
  void set_count(int n) {
    if(std::find(counts.begin(),counts.end(),n)==counts.end())throw std::invalid_argument("Unsupported particle count");
    auto initial=initial_particles(n);
    std::vector<std::pair<double,double>> drift(n+1);
    edit e;
    for(const auto& c:simulation.cells)for(auto p:c.particles) {
      e.remove.push_back({p.id,"x",particles::real(p.x)});
      e.remove.push_back({p.id,"y",particles::real(p.y)});
      e.remove.push_back({p.id,"region",integer(particles::cell_of(p.x,p.y))});
    }
    for(int i=0;i<n;i++) {
      auto id=uint32_t(i+1);auto x=initial[i*4],y=initial[i*4+1];
      e.add.push_back({id,"x",particles::real(x)});e.add.push_back({id,"y",particles::real(y)});
      e.add.push_back({id,"region",integer(particles::cell_of(x,y))});
      drift[id]={initial[i*4+2],initial[i*4+3]};
    }
    e.add.push_back({controls_entity,"simulation/count",integer(n)});
    simulation.modify(std::move(e));
    simulation.count=n;simulation.drift=std::move(drift);simulation.tick=0;last={};
  }
  void move_patch(int column,int row) {
    edit e;e.add={{controls_entity,"simulation/patch-column",integer(std::clamp(column,0,particles::cols-1))},
      {controls_entity,"simulation/patch-row",integer(std::clamp(row,0,particles::rows-1))},
      {controls_entity,"simulation/automatic",boolean(false)}};
    simulation.modify(std::move(e));
  }
  void step() {
    if(state.paused)return;
    edit e;
    // Automatic movement participates in the same transaction as this tick's
    // particle changes. The temporary physics input is published to UI on commit.
    if(state.local&&state.automatic&&simulation.tick>0&&simulation.tick%90==0) {
      simulation.patch_col=(state.column+1)%particles::cols;
      simulation.patch_row=(state.row+(simulation.patch_col==0))%particles::rows;
      e.add={{controls_entity,"simulation/patch-column",integer(simulation.patch_col)},
             {controls_entity,"simulation/patch-row",integer(simulation.patch_row)}};
    }
    last=simulation.step(std::move(e));
  }
  uint64_t button_builds() const {
    uint64_t n=0;for(const auto& b:buttons)n+=b.builds;return n;
  }
  void validate() {
    simulation.validate(6);
    if(state.count!=simulation.count)throw std::runtime_error("Control/particle count mismatch");
    if(buttons[6].selected!=state.paused||buttons[4].selected!=state.local||buttons[5].selected==state.local)
      throw std::runtime_error("Button publication mismatch");
    for(size_t i=0;i<counts.size();i++)if(buttons[i].selected!=(counts[i]==state.count))
      throw std::runtime_error("Count button publication mismatch");
  }
};
} // namespace demo
