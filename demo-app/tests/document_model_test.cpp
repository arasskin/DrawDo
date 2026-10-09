#include "document_model.h"
#include <iostream>
#include <unistd.h>
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){
  auto root=std::filesystem::temp_directory_path()/("doc-state-"+std::to_string(getpid()));
  try {
    datafriend::identity a("test",true),b("test",true);
    document::configuration config;config.peer=datafriend::public_key_hex(b);config.room="state-fact";
    {
      document::model m(root.string(),config,a);m.set_online(false);
      check(m.view().size()==1&&m.text().empty(),"empty document is one fact");
      m.replace(std::string(1000,'x'));m.replace("Hello 🌍");
      auto draft=m.editor();m.replace(draft,"Hello 🌍!");m.replace(draft,"Hello 🌍!!");
      check(m.text()=="Hello 🌍!!","repeated editor changes");
      check(m.own.export_facts().size()==2,"state and text only: no per-character facts");
      check(m.own.last_recomputed()==2,"only two fixed fact subscriptions maintained");
      check(m.view().size()==1,"UI remains one fact after edits");m.own.flush();
    }
    {
      document::model m(root.string(),config,a);m.set_online(false);
      check(m.text()=="Hello 🌍!!","restore document from persisted Yrs state");
      m.replace("");check(m.text().empty()&&m.view().size()==1,"empty replacement keeps UI fact");
      m.own.flush();
    }
    {document::model m(root.string(),config,a);check(m.text().empty(),"deletion survives restart");}
    std::filesystem::remove_all(root);
    std::cout<<"PASS: one-fact UI, bounded storage/subscriptions, repeated editor and restart including deletion\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::filesystem::remove_all(root);return 1;}
}
