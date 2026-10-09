#pragma once
#include "engine.h"
#include "identity.h"
#include <chrono>

namespace datafriend {
// All methods/callbacks execute on the caller. Stores and identities must outlive
// the connection. Publication callbacks must not destroy the connection/stores
// or recursively pump the connection. They may read views and edit other stores.
// One connection owns the sole sync exchange for each attached store.
class network {
  struct impl;
  std::unique_ptr<impl> p_;
public:
  struct request_timing {
    uint32_t request=0,channel=0;
    uint8_t operation=0;
    uint64_t elapsed_us=0,queue_us=0; // op 6 has no reply: elapsed_us is unused
    uint64_t snapshot_revision=0,enqueued_us=0,sent_us=0; // steady-clock epoch
  };
  struct statistics {
    uint64_t connections=0,requests=0,exchanges=0,restarts=0,signed_batches=0;
    uint64_t sent=0,received=0,reconcile_us=0,sign_us=0,apply_us=0;
    uint64_t request_us=0,request_count=0;
    uint64_t optimistic_uploads=0,optimistic_publications=0,optimistic_fallbacks=0; // uploads queued, not confirmed
    std::vector<request_timing> request_samples;
  };
  // Numeric IPv4/IPv6 address: avoids synchronous DNS on the application thread.
  network(std::string address,uint16_t port);
  ~network();
  network(const network&)=delete;
  network& operator=(const network&)=delete;
  uint32_t push(engine&,identity&);
  uint32_t subscribe(engine&,std::string public_key_hex,
                     std::function<void(const std::vector<uint64_t>&)> published={});
  // Nonblocking socket progress; soft time budget between protocol operations.
  // A single reconciliation/apply cannot be preempted. Edits schedule themselves
  // via the store revision; pumping an idle connection does no database scan.
  void pump(std::chrono::microseconds budget=std::chrono::microseconds(1500));
  // Optional diagnostics: retain at most 4096 completed request timings. A
  // positive interval also sends read-only heartbeats on the same connection.
  // Queue time ends when the full request has been handed to the socket.
  void measure_requests(std::chrono::milliseconds heartbeat_interval={});
  void online(bool);
  bool connected() const;
  // Local output drained and no reconciliation pending; not a delivery receipt.
  bool settled() const;
  const std::string& error() const;
  const statistics& stats() const;
};
std::string public_key_hex(const identity&);
}
