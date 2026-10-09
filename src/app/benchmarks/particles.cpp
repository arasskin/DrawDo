// Matched native benchmark adapter; simulation is shared with the Sokol app.
#include "../../../demo-app/src/particle_simulation.h"
using namespace particles;

extern "C" int particle_pin(int cpu) {
#ifdef __ANDROID__
  cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(cpu,&mask);return sched_setaffinity(0,sizeof(mask),&mask);
#else
  (void)cpu;return 0;
#endif
}
extern "C" int particle_run(const char* directory,const double* initial,int count,int local,int warmup,int samples,const char* output) {
  try {
    simulation s(directory,initial,count,local);s.validate();std::vector<sample> timings;
    for(int i=0;i<warmup+samples;i++){auto row=s.step();if(i>=warmup)timings.push_back(row);}
    auto all=s.validate();std::ofstream out(output);out<<std::setprecision(17)<<"{\"variant\":\"native\",\"rows\":[";
    bool first=true;
    for(auto r:timings) {
      if(!first)out<<',';first=false;
      out<<"{\"physics_us\":"<<r.physics<<",\"triple_prepare_us\":"<<r.prepare<<",\"edit_us\":"<<r.edit
         <<",\"readback_us\":"<<r.readback<<",\"step_us\":"<<r.total<<",\"moved\":"<<r.moved
         <<",\"crossings\":"<<r.crossings<<",\"changed_views\":"<<r.changed<<",\"read_entities\":"<<r.reads
         <<",\"candidate_pairs\":"<<r.pairs<<",\"interactions\":"<<r.interactions<<'}';
    }
    out<<"],\"state\":[";first=true;
    for(auto p:all){if(!first)out<<',';first=false;out<<'['<<p.id<<','<<p.x<<','<<p.y<<']';}
    out<<"],\"checks\":\"passed\"}\n";if(!out)throw std::runtime_error("write failed");return 0;
  } catch(const std::exception& e) {
    std::ofstream out(output);out<<"{\"error\":\"native particle benchmark failed\"}\n";
    fprintf(stderr,"particle benchmark: %s\n",e.what());return 1;
  }
}
