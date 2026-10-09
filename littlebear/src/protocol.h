#pragma once
#include "codec.h"

// LBS5 envelopes only. No storage, authentication, or reconciliation dependencies.
namespace lb_sync {
using wire::reader;
using wire::writer;
constexpr size_t max_frame = 8 * 1024 * 1024;
constexpr size_t max_packet = max_frame - 2048;
constexpr size_t max_records = 1000000;
inline uint64_t integer(std::string_view bytes) { return reader{bytes}.u64(); }

// Views borrow the input frame; dispatch copies payloads needed by asynchronous I/O.
struct request {
  uint32_t id, channel;
  uint8_t op;
  std::string_view public_key, payload;
  uint64_t epoch, revision;
  std::string_view challenge, signature, signed_bytes;
  uint64_t sequence = 0;
};
inline request parse(std::string_view packet, std::string_view schema) {
  reader r{packet};
  if(r.take(4) != "LBS\5") throw std::invalid_argument("unsupported protocol");
  request out{};
  out.id = r.u32(); out.channel = r.u32(); out.op = r.u8();
  if(!out.id || out.op > 8) throw std::invalid_argument("invalid operation");
  out.public_key = r.take(r.u32());
  if(r.take(r.u32()) != schema) throw std::invalid_argument("invalid schema");
  if(out.op == 7) {
    if(out.channel || !out.public_key.empty()) throw std::invalid_argument("invalid heartbeat");
  } else if(!out.channel || out.public_key.size() != 32) {
    throw std::invalid_argument("invalid namespace/channel");
  }
  out.epoch = r.u64(); out.revision = r.u64(); out.payload = r.take(r.u32());
  if(out.op == 3 || out.op == 6) {
    out.challenge = r.take(32); out.sequence = r.u64();
    out.signature = r.take(64); out.signed_bytes = packet.substr(0, packet.size() - 64);
  }
  r.end();
  if((out.op == 4 || out.op == 5 || out.op >= 7) && !out.payload.empty()) throw std::invalid_argument("unexpected payload");
  return out;
}
}
