#include "task_screen.h"
#include <filesystem>
#include <iostream>
#include <unistd.h>
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){
  auto path=std::filesystem::temp_directory_path()/("drawdo-tasks-"+std::to_string(getpid()));
  std::filesystem::remove_all(path);uint32_t saved=0;
  try{
    {
      tasks::screen app(path);check(app.ids.size()==12,"seeded tasks");app.mount(0,5);
      auto id=app.ids[0],other=app.ids[1];app.select(id);
      auto list=app.list_builds,row=app.row_builds,counter=app.counter_builds,detail=app.detail_builds;
      auto* stable=app.rows.at(id).get();auto other_builds=app.rows.at(other)->builds;
      stable->rename("Renamed 🌱");
      check(app.list_builds==list&&app.counter_builds==counter,"title does not rebuild list/counter");
      check(app.row_builds==row+1&&app.detail_builds==detail+1,"title updates row + detail");
      check(app.rows.at(other)->builds==other_builds,"unrelated row untouched");
      stable->toggle();check(app.list_builds==list,"All list unchanged by completion");
      check(app.counter_builds==counter+1&&app.detail->done,"counter and detail update");
      app.move(id,3);check(app.ids[3]==id&&app.rows.at(id).get()==stable,"reorder preserves row identity");
      app.set_filter("Active");check(std::find(app.ids.begin(),app.ids.end(),id)==app.ids.end(),"filter removes completed task");
      app.detail->toggle();check(std::find(app.ids.begin(),app.ids.end(),id)!=app.ids.end(),"task re-enters active list");
      app.set_filter("Completed");check(app.ids.empty()&&app.rows.empty(),"empty filter releases rows");
      app.set_filter("All");
      for(int i=0;i<30;i++)app.add("Extra "+std::to_string(i));
      app.mount(25,5);check(app.rows.size()==5&&app.db.bindings.size()==9,"only visible rows own subscriptions");
      // Multiple changed subscriptions can be unmounted while dispatching a batch.
      app.set_filter("Active");app.mount(0,5);auto batch=app.ids;
      std::vector<ui::fact> edits;for(auto e:batch)edits.push_back({e,tasks::A::done,true});
      app.db.edit(edits);check(app.ids.empty()&&app.rows.empty(),"safe unmount during publication");
      app.set_filter("All");saved=app.add("Persist me");app.select(saved);
      app.rename(saved,"Kept after reopen");
      auto deleted=app.ids[0];app.db.remove(deleted);app.rename(deleted,"Must not resurrect");
      check(std::find(app.ids.begin(),app.ids.end(),deleted)==app.ids.end(),"stale editor cannot resurrect deletion");
      auto before=app.ids.size();app.add("  \n ");check(app.ids.size()==before,"blank add ignored");
      std::string error;check(!ui::attempt(error,[]{throw std::runtime_error("failure");})&&!error.empty(),"attempt reports error");
      check(ui::attempt(error,[]{})&&error.empty(),"attempt clears error");
      // Exercise native hit testing and clipping without Android/GPU dependencies.
      tasks::view view(app);native_ui::surface canvas{400,760,
        [](auto,uint32_t,bool){},[](std::string,float,float,float,uint32_t,float){},[](auto){}};
      view.draw(canvas);view.down(280,196);view.up(280,196);
      check(app.filter=="Completed","filter hit target is not clipped by list");
      app.set_filter("All");view.draw(canvas);
      auto rev=app.db.engine.revision();view.down(100,340);view.move(100,280);view.up(100,280);
      check(app.db.engine.revision()==rev,"scroll does not activate row action");
      view.draw(canvas);
      app.db.engine.flush();
    }
    {tasks::screen app(path);check(app.selected==saved&&app.detail->title=="Kept after reopen","tasks and selection persist");}
    std::filesystem::remove_all(path);std::cout<<"PASS: narrow updates, scoped rows, ordering, filters, stale edits and persistence\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::filesystem::remove_all(path);return 1;}
}
