#include "document_crdt.h"
#include <iostream>
#include <random>
using namespace document;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){try{
  sequence seed;seed.replace("Hello 🌍");auto initial=seed.state();
  auto left=seed.fork(),right=seed.fork();
  left->replace("Hello brave 🌍");right->replace("Goodbye 🌍!");
  std::vector<std::string> states{initial,left->state(),right->state()};
  std::string expected;
  for(int n=0;n<100;n++){
    std::shuffle(states.begin(),states.end(),std::mt19937(n));sequence s;
    for(auto& state:states){s.merge(state);check(!s.merge(state),"duplicate update is a no-op");}
    if(!n)expected=s.text();check(s.text()==expected,"reordered states converge");
  }
  left->merge(right->state());right->merge(left->state());
  check(left->text()==right->text(),"concurrent replacements converge");
  auto draft=seed.fork();draft->replace("Hello 🌍?");
  auto changed=seed.fork();changed->replace("Hello brave 🌍");changed->merge(draft->state());
  check(changed->text().find("brave")!=std::string::npos,"stale editor retains unobserved inserts");
  draft->replace("Hello 🌍?!");changed->merge(draft->state());
  check(changed->text().find("brave")!=std::string::npos,"repeated stale editor retains remote edits");
  sequence unicode;
  for(auto text:{"🌍","🌏","é","ê","aéz","aêz","a😀z","a😁z","","\n世界\n"}) {
    unicode.replace(text);sequence copy;copy.merge(unicode.state());check(copy.text()==text,"UTF-8 boundaries survive round trip");
  }
  bool rejected=false;try{unicode.replace("\xc0\x80");}catch(...){rejected=true;}check(rejected,"reject invalid UTF-8");
  rejected=false;try{unicode.replace(std::string("a\0b",3));}catch(...){rejected=true;}check(rejected,"reject embedded NUL for C text API");
  sequence large;large.replace(std::string(20000,'x'));sequence copy;copy.merge(large.state());check(copy.text()==large.text(),"large document round trip");
  large.replace("");copy.merge(large.state());check(copy.text().empty(),"deletion-only state merges");
  check(!copy.merge(initial)||!copy.text().empty(),"independent state accepted");
  std::cout<<"PASS: Yrs concurrent/reordered/duplicate states, repeated stale editor, Unicode, deletion and 20K document\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
