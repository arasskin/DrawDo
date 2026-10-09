#include "datafriend/network.h"
#include "datafriend/wire_codec.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <deque>
#include <limits>

namespace datafriend {
namespace {
using clock=std::chrono::steady_clock;
uint64_t micros(clock::time_point t){return std::chrono::duration_cast<std::chrono::microseconds>(clock::now()-t).count();}
constexpr size_t frame_limit=8*1024*1024,queue_limit=2*frame_limit;
bytes key_bytes(std::string_view hex){
  if(hex.size()!=64)throw std::invalid_argument("Expected 64-digit public key");
  bytes b;for(size_t i=0;i<64;i+=2){unsigned n=0;for(size_t j=i;j<i+2;j++){auto c=hex[j];if(c>='0'&&c<='9')n=n*16+c-'0';else if(c>='a'&&c<='f')n=n*16+c-'a'+10;else throw std::invalid_argument("Invalid public key");}b+=char(n);}return b;
}
struct reply {
  uint32_t request,channel;uint8_t status;uint64_t epoch,revision;bytes challenge,payload;
  explicit reply(std::string_view b){reader r{b};request=r.u32();channel=r.u32();auto f=r.u8();status=f&127;epoch=r.u64();revision=r.u64();if(f&128)challenge=r.take(32);payload=r.data;}
};
}
std::string public_key_hex(const identity& key){constexpr char hex[]="0123456789abcdef";std::string out;for(auto b:key.public_key()){out+=hex[b>>4];out+=hex[b&15];}return out;}
struct network::impl {
  enum class phase {idle,start,reconcile,apply,subscribe,fetch};
  struct channel {
    engine* db;identity* signer;bytes key;std::function<void(const std::vector<uint64_t>&)> publish;
    phase state=phase::idle;uint32_t request=0;clock::time_point sent{},started{};
    uint64_t epoch=0,revision=0,sequence=0,pushed=UINT64_MAX,snapshot=0;
    bytes challenge;bool absent=false,subscribed=false,final_page=false;
    unsigned rounds=0;std::optional<reply> opening;bool superseded=false;
  };
  sockaddr_storage address{};socklen_t address_size=0;
  std::map<uint32_t,channel> channels;
  int fd=-1;bool connecting=false,enabled=true;uint32_t next_request=1,next_channel=1,heartbeat=0;
  struct outgoing {bytes data;uint32_t request;};
  struct pending_timing {request_timing sample;clock::time_point enqueued;};
  bytes input;std::deque<outgoing> output;size_t offset=0,queued=0;
  bool measuring=false;
  std::chrono::milliseconds heartbeat_interval{10000};
  std::map<uint32_t,pending_timing> timings;
  clock::time_point retry{},connected_at{},last_ping{},heartbeat_sent{};
  unsigned failures=0;std::string last_error;statistics stats;
  impl(std::string host,uint16_t port){
    if(!port)throw std::invalid_argument("Invalid port");
    auto* a=reinterpret_cast<sockaddr_in*>(&address);a->sin_family=AF_INET;a->sin_port=htons(port);
    if(inet_pton(AF_INET,host.c_str(),&a->sin_addr)==1){address_size=sizeof(*a);return;}
    auto* a6=reinterpret_cast<sockaddr_in6*>(&address);*a6={};a6->sin6_family=AF_INET6;a6->sin6_port=htons(port);
    if(inet_pton(AF_INET6,host.c_str(),&a6->sin6_addr)!=1)throw std::invalid_argument("Network endpoint must be a numeric IP address");
    address_size=sizeof(*a6);
  }
  ~impl(){reset();}
  void reset(){
    if(fd>=0)::close(fd);
    fd=-1;connecting=false;heartbeat=0;input.clear();output.clear();timings.clear();offset=queued=0;
    for(auto& [id,c]:channels){c.db->sync(4);c.state=phase::idle;c.request=0;c.pushed=UINT64_MAX;c.subscribed=false;c.opening.reset();c.superseded=false;}
  }
  void fail(std::string error){last_error=std::move(error);reset();retry=clock::now()+std::chrono::milliseconds(250u<<std::min(failures++,7u));}
  void connect(){
    fd=::socket(address.ss_family,SOCK_STREAM,0);if(fd<0)throw std::runtime_error("socket failed");
    if(fcntl(fd,F_SETFL,O_NONBLOCK)<0)throw std::runtime_error("nonblocking socket failed");
    int yes=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
#ifdef SO_NOSIGPIPE
    setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
#endif
    connected_at=clock::now();int rc=::connect(fd,reinterpret_cast<sockaddr*>(&address),address_size);
    if(rc<0&&errno!=EINPROGRESS)throw std::runtime_error(std::string("connect: ")+strerror(errno));
    connecting=true;
  }
  uint32_t send(uint32_t id,channel* c,uint8_t op,std::string_view payload){
    if(!next_request)throw std::runtime_error("Request counter exhausted");
    auto request=next_request++;writer w;w.data="LBS\5";w.u32(request);w.u32(id);w.u8(op);w.blob(c?c->key:bytes{});w.blob(attribute_schema);
    w.u64(c?c->epoch:0);w.u64(c?c->revision:0);w.blob(payload);
    if(op==3||op==6){
      if(!c->signer||c->challenge.size()!=32)throw std::runtime_error("Missing owner challenge");
      w.data+=c->challenge;w.u64(c->sequence++);auto start=clock::now();auto sig=c->signer->sign_write(w.data);stats.sign_us+=micros(start);
      w.data.append(reinterpret_cast<const char*>(sig.data()),sig.size());++stats.signed_batches;
    }
    if(w.data.size()>frame_limit)throw std::length_error("Network frame limit");
    writer frame;frame.blob(w.data);if(queued+frame.data.size()>queue_limit)throw std::length_error("Network queue limit");
    queued+=frame.data.size();output.push_back({std::move(frame.data),request});++stats.requests;
    if(measuring&&stats.request_samples.size()+timings.size()<4096){
      auto now=clock::now();auto stamp=std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
      timings.emplace(request,pending_timing{{request,id,op,0,0,c&&c->signer?c->snapshot:0,uint64_t(stamp),0},now});
    }
    if(c&&op!=6){c->request=request;c->sent=clock::now();}return request;
  }
  bytes native(channel& c,unsigned op,std::string_view packet={}){auto start=clock::now();auto result=c.db->sync(op,packet);stats.reconcile_us+=micros(start);return result;}
  void finish(channel& c){c.db->sync(4);c.state=phase::idle;if(c.signer)c.pushed=c.snapshot;++stats.exchanges;failures=0;last_error.clear();}
  void transfer(uint32_t id,channel& c){
    if(c.signer){auto changes=native(c,2);reader r{changes};if(r.u32()==0&&!c.absent){finish(c);return;}c.state=phase::apply;send(id,&c,3,changes);}
    else {auto keys=native(c,3);c.final_page=keys[0]==1;c.state=phase::fetch;send(id,&c,2,std::string_view(keys).substr(1));}
  }
  void reconcile(uint32_t id,channel& c,bytes packet){
    if(packet.size()==4){transfer(id,c);return;}
    if(++c.rounds>128)throw std::runtime_error("Reconciliation round limit");
    c.state=phase::reconcile;send(id,&c,1,packet);
  }
  void open(uint32_t id,channel& c,reply r){
    if(!r.challenge.empty())throw std::runtime_error("Challenge on replica");
    c.db->sync(4);c.state=phase::idle;c.superseded=false;
    if(r.status==3)return;
    if(r.status==1&&r.payload.empty())return;
    if(r.status==5){
      c.epoch=r.epoch;c.revision=r.revision;c.started=clock::now();c.rounds=0;
      auto start=clock::now();auto changed=c.db->sync_apply_delta(r.payload);stats.apply_us+=micros(start);
      if(changed){++stats.optimistic_publications;finish(c);if(c.publish)c.publish(*changed);return;}
      ++stats.optimistic_fallbacks;reconcile(id,c,native(c,0));return;
    }
    if(r.status!=0&&r.status!=1&&r.status!=4)throw std::runtime_error("Invalid subscription opening");
    c.epoch=r.epoch;c.revision=r.revision;c.started=clock::now();c.rounds=0;
    reconcile(id,c,native(c,5,r.payload));
  }
  void receive(reply r){
    if(auto it=timings.find(r.request);it!=timings.end()){
      it->second.sample.elapsed_us=micros(it->second.enqueued);
      stats.request_samples.push_back(it->second.sample);timings.erase(it);
    }
    if(r.channel==0){if(r.request!=heartbeat||r.status!=0)throw std::runtime_error("Invalid heartbeat");heartbeat=0;return;}
    auto it=channels.find(r.channel);if(it==channels.end())throw std::runtime_error("Unknown channel");auto& c=it->second;
    if(!r.request){
      if(c.signer){
        if(r.status!=6)throw std::runtime_error("Littlebear rejected streamed update");
        ++stats.optimistic_fallbacks;
        // A fresh start is an ordered barrier behind already queued writes.
        // It reconciles the latest local snapshot, not the failed old batch.
        // If a start is already in flight it provides that same barrier.
        if(!c.request){c.db->sync(4);c.state=phase::idle;c.pushed=UINT64_MAX;}
        return;
      }
      if(c.signer||(r.status!=3&&r.status!=4&&r.status!=5))throw std::runtime_error("Invalid unsolicited opening");
      // Coalesce openings while a correlated request is outstanding. Its reply is
      // drained before beginning a new snapshot, never applied to the new one.
      if(c.opening&&c.opening->epoch==r.epoch&&c.opening->revision>r.revision)return;
      c.opening=std::move(r);if(c.request)c.superseded=true;return;
    }
    if(r.request!=c.request)throw std::runtime_error("Reply request mismatch");
    c.request=0;stats.request_us+=micros(c.sent);++stats.request_count;
    if(c.superseded){c.db->sync(4);c.state=phase::idle;c.superseded=false;++stats.restarts;return;}
    if(!c.signer&&c.state==phase::subscribe){c.subscribed=true;open(r.channel,c,std::move(r));return;}
    if(!c.signer&&(r.status==1||r.status==3||r.epoch!=c.epoch||r.revision!=c.revision)){
      ++stats.restarts;c.db->sync(4);c.state=phase::idle;
      if(r.status==3||!r.payload.empty())c.opening=std::move(r);
      return;
    }
    if(c.signer&&r.status==1){++stats.restarts;c.db->sync(4);c.state=phase::idle;return;}
    if(r.status!=0&&!(c.state==phase::start&&r.status==3))throw std::runtime_error("Littlebear rejected request");
    if(!r.challenge.empty()){
      if(!c.signer||!c.challenge.empty()||!((c.state==phase::start&&r.status==3)||c.state==phase::reconcile))throw std::runtime_error("Unexpected write challenge");
      c.challenge=std::move(r.challenge);
    }
    c.epoch=r.epoch;c.revision=r.revision;
    switch(c.state){
      case phase::start:c.absent=r.status==3;c.snapshot=c.db->revision();reconcile(r.channel,c,native(c,0));break;
      case phase::reconcile:reconcile(r.channel,c,native(c,1,r.payload));break;
      case phase::apply:c.absent=false;transfer(r.channel,c);break;
      case phase::fetch:{auto start=clock::now();auto changed=c.db->sync_apply(r.payload);stats.apply_us+=micros(start);
        if(c.final_page){finish(c);if(c.publish)c.publish(changed);}else transfer(r.channel,c);break;}
      default:throw std::runtime_error("Unexpected response phase");
    }
  }
  void io(clock::time_point deadline){
    while(!output.empty()&&clock::now()<deadline){auto& b=output.front().data;
#ifdef MSG_NOSIGNAL
      constexpr int flags=MSG_NOSIGNAL;
#else
      constexpr int flags=0;
#endif
      auto n=::send(fd,b.data()+offset,b.size()-offset,flags);
      if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
      if(n<0&&errno==EINTR)continue;
      if(n<=0)throw std::runtime_error("Network send failed");
      stats.sent+=uint64_t(n);queued-=size_t(n);offset+=size_t(n);if(offset==b.size()){
        if(auto it=timings.find(output.front().request);it!=timings.end()){
          auto now=clock::now();it->second.sample.queue_us=std::chrono::duration_cast<std::chrono::microseconds>(now-it->second.enqueued).count();
          it->second.sample.sent_us=std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
          if(it->second.sample.operation==6){stats.request_samples.push_back(it->second.sample);timings.erase(it);}
        }
        output.pop_front();offset=0;
      }
    }
    while(clock::now()<deadline){
      if(input.size()>=4){reader r{input};auto n=r.u32();if(n<25||n>frame_limit)throw std::runtime_error("Invalid frame length");
        if(input.size()>=n+4){reply response(std::string_view(input).substr(4,n));input.erase(0,n+4);receive(std::move(response));continue;}}
      char buffer[16384];auto n=recv(fd,buffer,sizeof(buffer),0);
      if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
      if(n<0&&errno==EINTR)continue;
      if(n<=0)throw std::runtime_error("Network disconnected");
      stats.received+=uint64_t(n);input.append(buffer,size_t(n));if(input.size()>frame_limit+16384)throw std::length_error("Receive buffer limit");
    }
  }
  void pump(std::chrono::microseconds budget){
    if(!enabled||channels.empty())return;
    auto now=clock::now();auto deadline=now+budget;
    try {
      if(fd<0){if(now<retry)return;connect();}
      if(connecting){pollfd p{fd,POLLOUT,0};auto rc=::poll(&p,1,0);if(rc<0)throw std::runtime_error("Connect poll failed");if(!rc){if(now-connected_at>std::chrono::seconds(10))throw std::runtime_error("Connect timeout");return;}
        int error=0;socklen_t n=sizeof(error);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&n)||error)throw std::runtime_error("Connection refused/unavailable");
        connecting=false;++stats.connections;last_ping=now;last_error.clear();}
      io(deadline);
      for(auto& [id,c]:channels){
        if(clock::now()>=deadline)break;
        if(c.request){if(now-c.sent>std::chrono::seconds(10)||now-c.started>std::chrono::seconds(30))throw std::runtime_error("Sync timeout");continue;}
        if(!c.signer&&c.opening){auto r=std::move(*c.opening);c.opening.reset();open(id,c,std::move(r));continue;}
        if(c.state!=phase::idle)continue;
        if(c.signer&&c.db->revision()!=c.pushed){
          // Under backpressure coalesce later edits in the store's journal.
          if(queued>queue_limit-512*1024)continue;
          if(!c.challenge.empty())if(auto delta=c.db->sync_delta(c.pushed)){
            c.snapshot=c.db->revision();send(id,&c,6,*delta);
            ++stats.optimistic_uploads;finish(c);continue;
          }
          c.epoch=c.revision=c.sequence=0;c.challenge.clear();c.rounds=0;c.started=now;c.state=phase::start;send(id,&c,0,bytes(1,1));
        }
        else if(!c.signer&&!c.subscribed){c.epoch=c.revision=0;c.started=now;c.state=phase::subscribe;send(id,&c,8,{});}
      }
      if(heartbeat&&now-heartbeat_sent>std::chrono::seconds(10))throw std::runtime_error("Heartbeat timeout");
      if(!heartbeat&&now-last_ping>heartbeat_interval){heartbeat=send(0,nullptr,7,{});heartbeat_sent=last_ping=now;}
      io(deadline);
    }catch(const std::exception& e){fail(e.what());}
  }
  uint32_t attach(engine& db,identity* signer,std::string key,std::function<void(const std::vector<uint64_t>&)> callback){
    if(channels.size()>=64)throw std::length_error("Channel limit");
    for(auto& [id,c]:channels)if(c.db==&db)throw std::invalid_argument("Store already attached");
    if(db.namespace_name()!=key)throw std::invalid_argument("Store namespace must match public key");
    if(signer)db.sync_track();
    auto id=next_channel++;channels.emplace(id,channel{.db=&db,.signer=signer,.key=key_bytes(key),.publish=std::move(callback),.challenge={},.opening={}});return id;
  }
};
network::network(std::string address,uint16_t port):p_(std::make_unique<impl>(std::move(address),port)){}
network::~network()=default;
uint32_t network::push(engine& db,identity& key){return p_->attach(db,&key,public_key_hex(key),{});}
uint32_t network::subscribe(engine& db,std::string key,std::function<void(const std::vector<uint64_t>&)> f){return p_->attach(db,nullptr,std::move(key),std::move(f));}
void network::pump(std::chrono::microseconds budget){p_->pump(budget);}
void network::measure_requests(std::chrono::milliseconds interval){
  if(interval.count()<0)throw std::invalid_argument("Negative heartbeat interval");
  p_->stats.request_samples.reserve(4096);p_->measuring=true;
  p_->heartbeat_interval=interval.count()?interval:std::chrono::milliseconds(10000);
}
void network::online(bool enabled){if(p_->enabled==enabled)return;p_->enabled=enabled;p_->reset();p_->retry={};}
bool network::connected() const{return p_->fd>=0&&!p_->connecting;}
bool network::settled() const {if(!connected()||p_->queued)return false;for(auto& [id,c]:p_->channels)if(c.request||c.opening||c.state!=impl::phase::idle||(c.signer?c.pushed!=c.db->revision():!c.subscribed))return false;return true;}
const std::string& network::error() const{return p_->last_error;}
const network::statistics& network::stats() const{return p_->stats;}
}
