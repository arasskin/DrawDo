#include "document_model.h"
#include <iostream>
#include <thread>
#include <unistd.h>
int main(int argc,char** argv){try{
 if(argc!=3)throw std::runtime_error("usage: document_network_test HOST PORT");
 auto root=std::filesystem::temp_directory_path()/("doc-model-"+std::to_string(getpid()));std::filesystem::remove_all(root);
 datafriend::identity a("test",true),b("test",true);document::configuration ca,cb;ca.host=cb.host=argv[1];ca.port=cb.port=uint16_t(std::stoi(argv[2]));ca.room=cb.room="host";ca.peer=datafriend::public_key_hex(b);cb.peer=datafriend::public_key_hex(a);ca.seed=1000;cb.role=1;
 {document::model left((root/"a").string(),ca,a),right((root/"b").string(),cb,b);
 auto wait=[&](){auto end=document::time_us()+15000000;while(left.text()!=right.text()||!left.connection.settled()||!right.connection.settled()){left.pump();right.pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));if(document::time_us()>end)throw std::runtime_error("Convergence timeout: "+left.error+" / "+right.error);}};
 wait();if(left.view().size()!=1||right.view().size()!=1)throw std::runtime_error("UI must project one document fact");left.set_online(false);right.set_online(false);left.replace("abc 🌍");right.replace("def 🌏");left.set_online(true);right.set_online(true);wait();
 auto draft=left.editor();left.replace(draft,"first");right.replace(right.text()+" remote");wait();left.replace(draft,"first!");wait();if(left.text().find("remote")==std::string::npos)throw std::runtime_error("Stale draft erased remote insert");
 std::cout<<"PASS: document sync, 1000-char seed, concurrent offline replacement, live stale draft; text="<<left.text()<<'\n';}
 std::filesystem::remove_all(root);
 }catch(std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
