#pragma once
#include "document_crdt.h"
#include "datafriend/network.h"
#include "datafriend/wire_codec.h"
#include "profiling/clock_probe.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <sstream>

namespace document {
using clock_type=std::chrono::steady_clock;
inline int64_t time_us(){return std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now().time_since_epoch()).count();}
inline int64_t wall_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
inline std::string json_string(std::string_view value){
  std::string out="\"";constexpr char hex[]="0123456789abcdef";
  for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}return out+'"';
}
struct configuration {
  std::string host="192.168.1.19",peer,room="shared";uint16_t port=17879;int role=0,scenario=0,seed=0;int64_t start_ms=0;int duration_ms=30000,probe_interval_ms=0;
  std::string clock_host;unsigned clock_port=0;
  bool load(const std::string& directory){
    std::ifstream f(directory+"/document-config.txt");if(!f)return false;unsigned p;
    if(!(f>>host>>p>>peer>>room>>role>>scenario>>seed>>start_ms>>duration_ms)||!p||p>65535||peer.size()!=64||room.empty()||room.size()>64||room.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos||role<0||role>1||scenario<0||scenario>3||seed<0||seed>20000||duration_ms<5000||duration_ms>180000)throw std::runtime_error("Invalid document configuration");
    if(f>>probe_interval_ms)if(probe_interval_ms<0||probe_interval_ms>10000)throw std::runtime_error("Invalid probe interval");
    if(f>>clock_host>>clock_port)if(!clock_port||clock_port>65535)throw std::runtime_error("Invalid calibration port");
    port=uint16_t(p);return true;
  }
};
// One opaque Yrs state per owner namespace and
// one materialized text fact. Only the text fact is projected by the UI.
class model {
  static constexpr uint32_t document_id=1;
  uint64_t own_state_watch=0,peer_state_watch=0,text_watch=0;
  datafriend::query_result own_state,peer_state,document_text;
  uint64_t local_tick=0,peer_tick=0;
  std::string directory_;int action_index=0;bool went_offline=false,came_online=false;
  static datafriend::query field(std::string name) {
    return {{{{-1,datafriend::entity_value(document_id)},std::move(name),{0,{}}}},{0},false};
  }
  std::string state_record(uint64_t tick,std::string_view state) const {
    datafriend::writer w;w.data="YDF2";w.blob(config.room);w.u64(tick);w.blob(state);return w.data;
  }
  std::pair<uint64_t,std::string> state_value(const datafriend::query_result& rows) const {
    auto* bytes=rows.value(document_id,uint32_t(datafriend::attribute::documentState));
    if(!bytes)return {};
    datafriend::reader r{*bytes};if(r.take(4)!="YDF2")throw std::runtime_error("Unsupported document format");
    auto room=r.blob();auto tick=r.u64();auto state=r.blob();r.end();
    if(room!=config.room)return {};return {tick,std::move(state)};
  }
  void restore() {
    doc.reset();auto a=state_value(own_state),b=state_value(peer_state);
    if(!a.second.empty())doc.merge(a.second);
    if(!b.second.empty())doc.merge(b.second);
    local_tick=a.first;peer_tick=b.first;
  }
  // Snapshot and visible text are committed together. On an engine failure,
  // rebuild app CRDT state from the unchanged, atomic database snapshots.
  void publish(uint64_t tick,std::string state) {
    datafriend::edit edit;
    edit.add={{document_id,"document/state",state_record(tick,state)},
              {document_id,"document/text",doc.text()}};
    try { own.modify(std::move(edit)); }
    catch(...) { restore();throw; }
    local_tick=tick;
  }
  void remote(const std::vector<uint64_t>& changed) {
    auto contains=[&](uint64_t id){return std::find(changed.begin(),changed.end(),id)!=changed.end();};
    if(contains(peer_state_watch)) {
      auto start=time_us();auto [tick,state]=state_value(peer_state);
      if(!state.empty()) {
        auto previous=peer_tick;bool modified=doc.merge(state);peer_tick=std::max(peer_tick,tick);
        if(modified)publish(local_tick,doc.state());
        if(tick>previous)receives.push_back({time_us(),tick,tick-previous,start});
      }
      merge_us+=time_us()-start;
    }
  }
  template<class Edit> void change(Edit&& edit) {
    auto start=time_us();
    if(!edit())return;
    auto crdt_done=time_us();auto state=doc.state();auto encoded=time_us();
    auto tick=local_tick+1;publish(tick,std::move(state));auto committed=time_us();
    edits.push_back({start,committed-start,tick,1,crdt_done-start,encoded-crdt_done,committed-encoded,0,own.revision()});
  }
public:
  struct edit_sample{int64_t when,edit_us;uint64_t tick,operations;int64_t crdt_us,encode_us,commit_us,reserved_us;uint64_t revision;};
  struct receive_sample{int64_t when;uint64_t tick,operations;int64_t received;};
  configuration config;std::string actor,error;sequence doc;
  datafriend::engine own,peer;
  datafriend::network connection; // destroyed before stores
  clock_probe calibration;
  int64_t merge_us=0,pump_us=0;bool online=true,done=false;
  std::vector<edit_sample> edits;std::vector<receive_sample> receives;
  model(const std::string& directory,configuration cfg,datafriend::identity& identity):directory_(directory),config(std::move(cfg)),actor(datafriend::public_key_hex(identity)),
    own(directory+"/document-yrs-v2-"+config.room+"/own",datafriend::durability::asynchronous,1<<28,actor),
    peer(directory+"/document-yrs-v2-"+config.room+"/peer-"+config.peer,datafriend::durability::asynchronous,1<<28,config.peer),connection(config.host,config.port) {
    auto watch=[&](auto& store,const char* name,auto& id,auto& result){auto subscription=store.subscribe(field(name));id=subscription.first;result=std::move(subscription.second);};
    watch(own,"document/state",own_state_watch,own_state);
    watch(own,"document/text",text_watch,document_text);
    watch(peer,"document/state",peer_state_watch,peer_state);
    // Remove the retired benchmark fact from existing local demo stores.
    own.import_facts({{datafriend::pair_key(document_id,36),std::nullopt}});
    restore();publish(local_tick,doc.state());
    connection.push(own,identity);connection.subscribe(peer,config.peer,[this](const auto& c){remote(c);});
    if(config.probe_interval_ms||config.clock_port)connection.measure_requests(std::chrono::milliseconds(config.probe_interval_ms));
    if(config.clock_port)calibration.open(config.clock_host,uint16_t(config.clock_port));
    if(config.role==0&&config.seed&&local_tick==0&&peer_tick==0)replace(std::string(config.seed,'x'));
  }
  // The view has exactly one fact, including for an empty document. Borrow it
  // only until the next edit/pump, just like any other Datafriend result.
  const std::string& text() const {
    auto* value=document_text.value(document_id,uint32_t(datafriend::attribute::documentText));
    if(!value)throw std::runtime_error("Missing document text");return *value;
  }
  const datafriend::query_result& view() const { return document_text; }
  uint64_t edit_count() const { return local_tick+peer_tick; }
  uint64_t remote_tick() const { return peer_tick; }
  void replace(std::string next) { change([&]{return doc.replace(next);}); }
  std::shared_ptr<sequence> editor() const { return doc.fork(); }
  void replace(const std::shared_ptr<sequence>& draft,std::string next) {
    // The native editor retains its observed version. Yrs merges edits made on
    // that version without deleting inserts the editor has never seen.
    auto before=draft->state();
    try { change([&]{if(!draft->replace(next))return false;return doc.merge(draft->state());}); }
    catch(...) {draft->reset();draft->merge(before);throw;}
  }
  void set_online(bool value){online=value;connection.online(value);}
  void pump(){
    calibration.pump();
    auto start=time_us();connection.pump();pump_us+=time_us()-start;error=connection.error();
    if(!config.start_ms||done)return;
    auto elapsed=wall_ms()-config.start_ms;
    if(elapsed<0)return;
    if(config.scenario==3){if(elapsed>=2000&&!went_offline){set_online(false);went_offline=true;}if(elapsed>=7000&&!came_online){set_online(true);came_online=true;}}
    if(elapsed<10000&&(config.scenario>=2||config.role==0)){
      auto due=int(elapsed/150);
      if(action_index<=due){
        auto chars=scalars(text());std::string next;
        if(action_index%7==6&&!chars.empty()) {chars.pop_back();for(auto& c:chars)next+=c;}
        else {auto middle=chars.size()/2;for(size_t i=0;i<=chars.size();i++){if(i==middle)next+=config.role?"B":"a";if(i<chars.size())next+=chars[i];}}
        replace(std::move(next));++action_index;
      }
    }
    if(elapsed>=config.duration_ms)done=true;
  }
  void save(const std::string& extra){
    std::ofstream f(directory_+"/document-report.json.tmp");auto& s=connection.stats();
    f<<"{\"implementation\":\"yrs-0.24.0-state-fact-v2\",\"edit_component_names\":[\"crdt\",\"encode_state\",\"commit_publish\",\"reserved\"],\"room\":"<<json_string(config.room)<<",\"role\":"<<config.role<<",\"scenario\":"<<config.scenario<<",\"seed\":"<<config.seed<<",\"text\":"<<json_string(text())<<",\"operations\":"<<edit_count()<<",\"state_bytes\":"<<doc.state().size()<<",\"ui_facts\":"<<view().size()<<",\"connected\":"<<connection.connected()<<",\"settled\":"<<connection.settled()<<",\"error\":"<<json_string(error)
      <<",\"network\":{\"sent\":"<<s.sent<<",\"received\":"<<s.received<<",\"requests\":"<<s.requests<<",\"exchanges\":"<<s.exchanges<<",\"restarts\":"<<s.restarts<<",\"connections\":"<<s.connections<<",\"signed_batches\":"<<s.signed_batches<<",\"reconcile_us\":"<<s.reconcile_us<<",\"sign_us\":"<<s.sign_us<<",\"apply_us\":"<<s.apply_us<<",\"merge_us\":"<<merge_us<<",\"pump_us\":"<<pump_us<<",\"request_us\":"<<s.request_us<<",\"request_count\":"<<s.request_count<<"},\"edits\":[";
    bool first=true;for(auto e:edits){if(!first)f<<',';first=false;f<<'['<<e.when<<','<<e.edit_us<<','<<e.tick<<','<<e.operations<<','<<e.crdt_us<<','<<e.encode_us<<','<<e.commit_us<<','<<e.reserved_us<<','<<e.revision<<']';}
    f<<"],\"receives\":[";first=true;for(auto e:receives){if(!first)f<<',';first=false;f<<'['<<e.when<<','<<e.tick<<','<<e.operations<<','<<e.received<<']';}
    f<<"],\"optimistic_sync\":{\"uploads\":"<<s.optimistic_uploads<<",\"publications\":"<<s.optimistic_publications<<",\"fallbacks\":"<<s.optimistic_fallbacks<<"},\"request_timings\":[";first=true;for(auto sample:s.request_samples){if(!first)f<<',';first=false;f<<'['<<sample.request<<','<<sample.channel<<','<<unsigned(sample.operation)<<','<<sample.elapsed_us<<','<<sample.queue_us<<','<<sample.snapshot_revision<<','<<sample.enqueued_us<<','<<sample.sent_us<<']';}
    f<<"],"<<calibration.json()<<','<<extra<<"}\n";f.close();if(!f)throw std::runtime_error("Document report write failed");std::filesystem::rename(directory_+"/document-report.json.tmp",directory_+"/document-report.json");
  }
};
}
