#pragma once
#include "datafriend/wire_codec.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <array>
#include <vector>
#include <sstream>

namespace document {
// Benchmark-only NTP-style calibration against one LAN clock. Neither changes
// the phone's clock nor carries document data. Pumped on the existing UI context.
class clock_probe {
  int fd=-1;
  uint64_t pending=0,last=0;
  std::vector<std::array<uint64_t,4>> samples;
  static uint64_t now(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
public:
  ~clock_probe(){if(fd>=0)::close(fd);}
  clock_probe()=default;
  clock_probe(const clock_probe&)=delete;
  void open(const std::string& host,uint16_t port){
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);
    if(inet_pton(AF_INET,host.c_str(),&address.sin_addr)!=1)throw std::invalid_argument("Invalid calibration address");
    fd=socket(AF_INET,SOCK_DGRAM,0);
    if(fd<0||fcntl(fd,F_SETFL,O_NONBLOCK)<0||connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof address))
      throw std::runtime_error("Clock calibration socket failed");
    samples.reserve(1024);
  }
  void pump(){
    if(fd<0)return;
    for(int i=0;i<8;++i){
      char bytes[64];auto size=recv(fd,bytes,sizeof bytes,0);auto t4=now();
      if(size<0)break;
      if(size!=28||std::string_view(bytes,4)!="DCLK")continue;
      datafriend::reader r{{bytes+4,24}};auto t1=r.u64(),t2=r.u64(),t3=r.u64();
      if(t1!=pending||t3<t2||t4<t1||t4-t1<t3-t2)continue;
      if(samples.size()<1024)samples.push_back({t1,t2,t3,t4});pending=0;
    }
    auto t=now();if(pending&&t-pending>1000000)pending=0;
    if(!pending&&t-last>=100000&&samples.size()<1024){
      datafriend::writer w;w.data="DCLK";w.u64(t);
      if(send(fd,w.data.data(),w.data.size(),0)==ssize_t(w.data.size()))pending=last=t;
    }
  }
  std::string json()const{
    std::ostringstream out;out<<"\"clock_samples\":[";bool first=true;
    for(auto s:samples){if(!first)out<<',';first=false;out<<'['<<s[0]<<','<<s[1]<<','<<s[2]<<','<<s[3]<<']';}
    out<<']';return out.str();
  }
};
}
