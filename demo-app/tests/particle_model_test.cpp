#include "particle_model.h"
#include <filesystem>
#include <iostream>
#include <unistd.h>

void require(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
int main() {
  auto path=std::filesystem::temp_directory_path()/("drawdo-demo-test-"+std::to_string(getpid()));
  std::filesystem::remove_all(path);
  try {
    {
      demo::app app(path.c_str());app.validate();
      auto cell_handle=app.simulation.cells[0].id;
      auto cell_result=app.simulation.cells[0].result;
      require(app.button_builds()==7,"one initial build per button");
      auto revision=app.simulation.db.revision();
      app.buttons[6].press();
      require(app.simulation.db.revision()==revision+1,"pause is a database edit");
      require(app.state.paused&&app.buttons[6].label=="Resume","pause publishes before handler returns");
      require(app.button_builds()==8,"pause only rebuilds one button");
      app.step();require(app.simulation.db.revision()==revision+1,"paused frame submits no edit");
      app.buttons[5].press();
      require(!app.state.local&&app.buttons[5].selected,"mode publishes synchronously");
      require(app.button_builds()==10,"mode only rebuilds two buttons");
      app.move_patch(7,11);
      require(app.state.column==7&&app.state.row==11&&!app.state.automatic,"drag stores patch state");
      require(app.button_builds()==10,"drag rebuilds no buttons");
      // Observe count publication independently: all particle views must already
      // reflect the replacement when the setting's subscriber runs.
      app.views.watch(demo::settings_query({"simulation/count"}),[&](const auto& r){
        size_t total=0;for(const auto& c:app.simulation.cells)total+=c.particles.size();
        require(total==demo::number(r,"simulation/count"),"count and particle views published atomically");
      });
      for(auto n:{3000,100,300,1000}) {
        revision=app.simulation.db.revision();auto builds=app.button_builds();
        app.set_count(n);app.validate();
        require(app.simulation.db.revision()==revision+1,"replacement uses one transaction");
        require(app.state.paused&&!app.state.local&&app.state.column==7,"replacement preserves controls");
        require(app.button_builds()==builds+4,"replacement only rebuilds count buttons");
        require(app.simulation.cells[0].id==cell_handle&&app.simulation.cells[0].result.shares_with(cell_result),
                "replacement preserves live query identity");
      }
      app.set_count(100);auto builds=app.button_builds();
      // Changes arriving through the store, without a button handler, also update UI.
      datafriend::edit e;e.add={{demo::controls_entity,"simulation/paused",demo::boolean(false)}};
      app.simulation.modify(std::move(e));
      require(!app.state.paused&&app.buttons[6].label=="Pause","store changes drive controls");
      require(app.button_builds()==builds+1,"external pause edit only rebuilds pause button");
      builds=app.button_builds();
      for(int i=0;i<20;i++)app.step();
      app.validate();require(app.button_builds()==builds,"particle motion never rebuilds buttons");
      app.buttons[4].press();
      e.add={{demo::controls_entity,"simulation/automatic",demo::boolean(true)}};
      app.simulation.modify(std::move(e));
      while(app.simulation.tick<90)app.step();
      auto old_column=app.state.column;revision=app.simulation.db.revision();builds=app.button_builds();
      app.step();
      require(app.state.column==(old_column+1)%particles::cols,"automatic patch uses stored state");
      require(app.simulation.db.revision()==revision+1,"automatic patch and physics share one edit");
      require(app.button_builds()==builds,"automatic patch never rebuilds buttons");
      app.buttons[6].press();app.validate();app.simulation.db.flush();
    }
    {
      datafriend::engine reopened(path);
      auto id=reopened.subscribe(demo::settings_query({"simulation/paused","simulation/count"})).first;
      auto result=reopened.result(id);
      require(demo::number(result,"simulation/paused")==1&&demo::number(result,"simulation/count")==100,
              "controls survive database reopen");
      reopened.unsubscribe(id);
    }
    std::filesystem::remove_all(path);
    std::cout<<"PASS: synchronous publication, selective button updates, atomic replacement, stable views, stored controls, simulation consistency\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::filesystem::remove_all(path);return 1;}
}
