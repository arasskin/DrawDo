// Linux transport: one completion loop for sockets and storage. No TLS or poll.
#include "auth.h"
#include "sync.h"
#include "storage_uring.h"
#include "../../datafriend-benchmarks/include/datafriend/attributes.h"
#include <liburing.h>
#include <arpa/inet.h>
#include <sys/random.h>
#include <signal.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace server {
using clock_type = std::chrono::steady_clock;
constexpr size_t max_clients = 8, max_channels = 64;
struct client;
enum class operation { accept, receive, send, file, cancel_receive, cancel_send, cancel_accept };
struct completion { operation kind; client* owner = nullptr; };
enum class input_phase { header, body };
struct channel {
  uint32_t id=0;
  std::string name;
  auth::state auth;
  uint64_t announced=0;
  bool subscribed=false, known=false;
};
struct client {
  int fd=-1;
  bool closing=false, receiving=false, sending=false, file_pending=false;
  bool cancelling_receive=false, cancelling_send=false;
  bool cancelled_receive=false, cancelled_send=false;
  input_phase phase=input_phase::header;
  uint8_t header[4]{};
  size_t received=0,sent=0;
  std::string input,output;
  lb_sync::response_task task;
  channel channels[max_channels];
  channel* active=nullptr;
  uint32_t request_id=0, channel_id=0;
  uint8_t op=0;
  uint64_t generation=0;
  bool events_dirty=false;
  completion receive{operation::receive}, send{operation::send}, file{operation::file};
  completion cancel_receive{operation::cancel_receive}, cancel_send{operation::cancel_send};
  clock_type::time_point deadline{};
};
struct loop {
  io_uring ring{};
  int listener=-1;
  bool accepting=false,cancelling_accept=false,accept_cancel_requested=false;
  completion accept{operation::accept},cancel_accept{operation::cancel_accept};
  client clients[max_clients];
};
struct options { const char* values=nullptr; const char* bind="127.0.0.1"; int port=7778; };
volatile sig_atomic_t stopping=0;
void stop(int) { stopping=1; }
void reset(client& c) {
  if(c.receiving||c.sending||c.file_pending||c.cancelling_receive||c.cancelling_send) std::terminate();
  c.task={};
  if(c.fd>=0) ::close(c.fd);
  c=client{};
}
void submit_accept(loop& s) {
  if(s.accepting||stopping) return;
  auto* sqe=io_uring_get_sqe(&s.ring); if(!sqe) return;
  io_uring_prep_accept(sqe,s.listener,nullptr,nullptr,SOCK_CLOEXEC);
  io_uring_sqe_set_data(sqe,&s.accept); s.accepting=true;
}
void close_client(loop& s,client& c) {
  c.closing=true;
  auto cancel=[&](bool pending,bool& requested,bool& cancelling,completion& original,completion& tag) {
    if(pending&&!requested) if(auto* sqe=io_uring_get_sqe(&s.ring)) {
      io_uring_prep_cancel(sqe,&original,0); io_uring_sqe_set_data(sqe,&tag);
      requested=cancelling=true;
    }
  };
  cancel(c.receiving,c.cancelled_receive,c.cancelling_receive,c.receive,c.cancel_receive);
  cancel(c.sending,c.cancelled_send,c.cancelling_send,c.send,c.cancel_send);
  if(!c.file_pending) c.task={};
  if(!c.receiving&&!c.sending&&!c.file_pending&&!c.cancelling_receive&&!c.cancelling_send) reset(c);
}
void submit_file(loop& s,client& c,storage::request* request) {
  if(storage_uring::enqueue(&s.ring,request,&c.file)) c.file_pending=true;
}
void frame(client& c,uint32_t request,uint32_t id,std::string_view reply) {
  if(reply.size()+8>lb_sync::max_frame) throw std::length_error("response too large");
  lb_sync::writer out;
  out.u32(uint32_t(reply.size()+8)); out.u32(request); out.u32(id); out.data+=reply;
  c.output=std::move(out.data); c.sent=0;
}
void dispatch(client& c,lb_sync::state& service) {
  using namespace lb_sync;
  c.active=nullptr; c.request_id=c.channel_id=0; c.op=255;
  try {
    if(c.input.size()>=12) { reader r{std::string_view(c.input).substr(4)}; c.request_id=r.u32(); c.channel_id=r.u32(); }
    const auto request=lb_sync::parse(c.input,datafriend::attribute_schema);
    c.op=request.op;
    if(request.op==7) {
      writer reply; reply.u8(0); reply.u64(0); reply.u64(0);
      c.task=ready_reply(std::move(reply.data));
    } else {
      const auto name=auth::namespace_id(request.public_key);
      for(auto& ch:c.channels) if(ch.id==request.channel) { c.active=&ch; break; }
      if(!c.active) for(auto& ch:c.channels) if(!ch.id) {
        ch.id=request.channel; ch.name=name; c.active=&ch; break;
      }
      if(!c.active||c.active->name!=name) throw std::invalid_argument("channel limit or namespace changed");
      auto& proof=c.active->auth;
      if(request.op==3) auth::verify(&proof,request);
      if(request.op==4) c.task=ready_reply(lb_sync::opening(&service,name));
      else if(request.op==5) {
        writer reply; reply.u8(0); reply.u64(request.epoch); reply.u64(request.revision);
        c.task=ready_reply(std::move(reply.data));
      } else if(request.op<=1) {
        // These operations touch only memory. The server coordinates authentication
        // metadata; neither auth nor sync dispatches into the other module.
        auto reply=lb_sync::handle(&service,request.op,name,false,request.epoch,request.revision,request.payload);
        const auto status=uint8_t(reply[0]);
        if(request.op==0&&(status==0||status==3)) {
          proof={}; proof.pushing=request.payload[0]==1;
          if(proof.pushing&&status==3) auth::attach_challenge(&proof,reply);
        } else if(request.op==1&&status==0&&proof.pushing&&!proof.issued&&
                  !equal_reconciliation(request.payload,std::string_view(reply).substr(17))) {
          auth::attach_challenge(&proof,reply);
        }
        c.task=ready_reply(std::move(reply));
      } else {
        c.task=lb_sync::handle_async(&service,request.op,name,request.op==3,
          request.epoch,request.revision,std::string(request.payload));
      }
    }
  } catch(const std::exception&) { c.task=rejected_response(); }
  c.input.clear(); c.received=0; c.phase=input_phase::header;
}
void finish(client& c,std::string& reply) {
  const auto status=uint8_t(reply[0])&0x7f;
  auto* ch=c.active;
  if(ch) {
    if(c.op==4&&status!=2) { ch->subscribed=true; c.events_dirty=true; }
    if(ch->subscribed&&(status==3||((c.op==4||status==1)&&reply.size()>17))) {
      lb_sync::reader r{std::string_view(reply).substr(9)};
      ch->announced=r.u64(); ch->known=true;
    }
    if(c.op==5&&status==0) *ch=channel{};
  }
  frame(c,c.request_id,c.channel_id,reply);
  c.active=nullptr;
}
void event(client& c,lb_sync::state& service) {
  if(!c.events_dirty&&c.generation==lb_sync::generation(&service)) return;
  c.events_dirty=true;
  for(auto& ch:c.channels) if(ch.subscribed&&!lb_sync::writing(&service,ch.name)) {
    auto revision=lb_sync::revision(&service,ch.name);
    if(ch.known&&ch.announced==revision) continue;
    auto reply=lb_sync::opening(&service,ch.name,4);
    frame(c,0,ch.id,reply);
    ch.known=true; ch.announced=revision;
    return; // One bounded output buffer; further updates coalesce by revision.
  }
  c.events_dirty=false; c.generation=lb_sync::generation(&service);
}
void advance(loop& s,client& c,lb_sync::state& service) {
  if(c.fd<0) return;
  if(stopping||clock_type::now()>c.deadline) c.closing=true;
  if(c.closing) { close_client(s,c); return; }
  if(c.file_pending) return;
  if(!c.sending&&c.output.empty()&&!c.task&&c.phase==input_phase::body&&c.received==c.input.size()) dispatch(c,service);
  while(c.task) {
    auto* request=c.task.pending();
    if(request&&request->state==storage::phase::ready) { submit_file(s,c,request); return; }
    if(request&&request->state!=storage::phase::finished) std::terminate();
    if(!c.task.done()) c.task.resume();
    if(c.task.done()) {
      std::string reply;
      try { reply=c.task.result(); }
      catch(const std::exception&) { auto rejected=lb_sync::rejected_response(); rejected.resume(); reply=rejected.result(); }
      c.task={}; finish(c,reply);
    }
  }
  if(!c.sending&&c.output.empty()) event(c,service);
  if(!c.output.empty()&&!c.sending) if(auto* sqe=io_uring_get_sqe(&s.ring)) {
    io_uring_prep_send(sqe,c.fd,c.output.data()+c.sent,c.output.size()-c.sent,MSG_NOSIGNAL);
    io_uring_sqe_set_data(sqe,&c.send); c.sending=true;
  }
  // Independent receive/send completions allow server openings on idle sockets.
  // Only one complete request is buffered, even when clients pipeline channels.
  const size_t length=c.phase==input_phase::header?sizeof c.header:c.input.size();
  if(!c.receiving&&c.received<length) if(auto* sqe=io_uring_get_sqe(&s.ring)) {
    void* buffer=c.phase==input_phase::header?static_cast<void*>(c.header+c.received):static_cast<void*>(c.input.data()+c.received);
    io_uring_prep_recv(sqe,c.fd,buffer,length-c.received,0);
    io_uring_sqe_set_data(sqe,&c.receive); c.receiving=true;
  }
}
void complete(loop& s,completion& tag,int result) {
  if(tag.kind==operation::accept) {
    s.accepting=false;
    if(result<0) return;
    if(!stopping) for(auto& c:s.clients) if(c.fd<0) {
      c.fd=result;
      c.receive.owner=c.send.owner=c.file.owner=c.cancel_receive.owner=c.cancel_send.owner=&c;
      c.deadline=clock_type::now()+std::chrono::seconds(30);
      return;
    }
    ::close(result); return;
  }
  if(tag.kind==operation::cancel_accept) { s.cancelling_accept=false; return; }
  auto& c=*tag.owner;
  if(tag.kind==operation::cancel_receive) { c.cancelling_receive=false; return; }
  if(tag.kind==operation::cancel_send) { c.cancelling_send=false; return; }
  if(tag.kind==operation::file) { c.file_pending=false; storage::complete_io(c.task.pending(),result); return; }
  if(tag.kind==operation::receive) c.receiving=false; else c.sending=false;
  if(c.closing) return;
  if(result==-EINTR||result==-EAGAIN) return;
  if(result<=0) { c.closing=true; return; }
  c.deadline=clock_type::now()+std::chrono::seconds(30);
  if(tag.kind==operation::send) {
    c.sent+=size_t(result);
    if(c.sent==c.output.size()) { c.output.clear(); c.sent=0; }
    return;
  }
  c.received+=size_t(result);
  if(c.phase==input_phase::header&&c.received==sizeof c.header) {
    uint32_t length=0; for(auto byte:c.header) length=(length<<8)|byte;
    if(length<13||length>lb_sync::max_frame) { c.closing=true; return; }
    try { c.input.resize(length); } catch(const std::exception&) { c.closing=true; return; }
    c.received=0; c.phase=input_phase::body;
  }
}
void run(loop& s,lb_sync::state& service) {
  for(;;) {
    if(stopping&&s.accepting&&!s.accept_cancel_requested) if(auto* sqe=io_uring_get_sqe(&s.ring)) {
      io_uring_prep_cancel(sqe,&s.accept,0); io_uring_sqe_set_data(sqe,&s.cancel_accept);
      s.cancelling_accept=s.accept_cancel_requested=true;
    }
    submit_accept(s);
    bool clients=false;
    for(auto& c:s.clients) {
      try { advance(s,c,service); } catch(const std::exception&) { if(c.fd>=0) close_client(s,c); }
      clients|=c.fd>=0;
    }
    if(stopping&&!clients&&!s.accepting&&!s.cancelling_accept) return;
    int submitted=io_uring_submit(&s.ring);
    if(submitted<0&&submitted!=-EINTR) { std::fprintf(stderr,"io_uring submission failed: %d\n",submitted); std::_Exit(2); }
    io_uring_cqe* cqe=nullptr; __kernel_timespec timeout{0,100000000};
    int waited=io_uring_wait_cqe_timeout(&s.ring,&cqe,&timeout);
    if(waited==-ETIME||waited==-EINTR) continue;
    if(waited<0) { std::fprintf(stderr,"io_uring wait failed: %d\n",waited); std::_Exit(2); }
    do {
      auto* tag=static_cast<completion*>(io_uring_cqe_get_data(cqe)); int result=cqe->res;
      io_uring_cqe_seen(&s.ring,cqe); if(tag) complete(s,*tag,result);
    } while(io_uring_peek_cqe(&s.ring,&cqe)==0);
  }
}
options parse_options(int argc, char** argv) {
  options out;
  for(int i = 1; i < argc; i += 2) {
    if(i + 1 == argc) throw std::invalid_argument("expected --option value");
    if(!std::strcmp(argv[i], "--values")) out.values = argv[i + 1];
    else if(!std::strcmp(argv[i], "--bind")) out.bind = argv[i + 1];
    else if(!std::strcmp(argv[i], "--port")) {
      char* end = nullptr; errno = 0;
      long port = std::strtol(argv[i + 1], &end, 10);
      if(errno || *end || end == argv[i + 1] || port < 0 || port > 65535) throw std::invalid_argument("invalid port");
      out.port = int(port);
    } else throw std::invalid_argument("unknown option; expected --values, --bind, --port");
  }
  if(!out.values) throw std::invalid_argument("required: --values");
  return out;
}
uint64_t boot_nonce() {
  uint64_t nonce;
  size_t done = 0;
  while(done != sizeof(nonce)) {
    auto n = getrandom(reinterpret_cast<char*>(&nonce) + done, sizeof(nonce) - done, 0);
    if(n < 0 && errno == EINTR) continue;
    if(n <= 0) throw std::runtime_error("randomness unavailable");
    done += size_t(n);
  }
  return nonce;
}
}
int main(int argc, char** argv) {
  using namespace server;
  try {
    auto args = parse_options(argc, argv);
    if(sodium_init() < 0) throw std::runtime_error("cryptography initialization failed");
    lb_sync::state service;
    lb_sync::create(&service,args.values,boot_nonce());
    loop s;
    io_uring_params params{};
    params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN | IORING_SETUP_DEFER_TASKRUN;
    if(io_uring_queue_init_params(64, &s.ring, &params) < 0) throw std::runtime_error("io_uring setup failed");
    s.listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(s.listener < 0) throw std::runtime_error("socket failed");
    int reuse = 1; setsockopt(s.listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(uint16_t(args.port));
    if(inet_pton(AF_INET, args.bind, &address.sin_addr) != 1 ||
       bind(s.listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(s.listener, 16))
      throw std::runtime_error("listen failed");
    socklen_t size = sizeof(address); getsockname(s.listener, reinterpret_cast<sockaddr*>(&address), &size);
    signal(SIGPIPE, SIG_IGN); signal(SIGTERM, stop); signal(SIGINT, stop);
    std::printf("Littlebear TCP listening %s:%u\n", args.bind, unsigned(ntohs(address.sin_port))); std::fflush(stdout);
    run(s, service);
    ::close(s.listener); io_uring_queue_exit(&s.ring);
  } catch(const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
