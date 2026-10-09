#pragma once
#include "reconcile.h"
#include "protocol.h"
#include <set>

// Shared transport-independent framing. The TCP driver bounds packets before allocation.
namespace lb_sync {
inline std::string begin(reconciliation::source source) {
  reconciliation::workspace work{}; uint8_t buffer[4096];
  reconciliation::output out{buffer,sizeof buffer};
  if(reconciliation::begin(source,&work,&out)!=reconciliation::status::complete)
    throw std::runtime_error("reconciliation source failure");
  return {reinterpret_cast<char*>(buffer),out.size};
}
inline std::string receive(reconciliation::source source, reconciliation::role role,
                           std::string_view packet, std::set<uint64_t>* have=nullptr,
                           std::set<uint64_t>* need=nullptr) {
  namespace r=reconciliation;
  r::workspace work{}; r::cursor cursor{}; uint8_t buffer[4096];
  uint64_t h[32],n[32]; r::differences diff{h,32,0,n,32,0};
  r::output out{buffer,sizeof buffer}; std::string reply("LBR\3",4);
  for(size_t turns=0;;++turns) {
    if(turns>max_records) throw std::length_error("reconciliation work limit");
    auto status=r::receive(source,role,{reinterpret_cast<const uint8_t*>(packet.data()),packet.size()},
                           &cursor,&work,&out,&diff);
    if(status!=r::status::complete&&status!=r::status::more&&status!=r::status::output_full&&status!=r::status::differences_full)
      throw std::invalid_argument("invalid reconciliation packet or source");
    if(out.size>4) reply.append(reinterpret_cast<char*>(buffer+4),out.size-4);
    if(have) have->insert(h,h+diff.have_count);
    if(need) need->insert(n,n+diff.need_count);
    if(reply.size()>max_packet||(have&&have->size()>max_records)||(need&&need->size()>max_records))
      throw std::length_error("reconciliation result limit");
    if(status==r::status::complete) return reply;
  }
}
inline bool equal_reconciliation(std::string_view request, std::string_view reply) {
  if(reply == std::string_view("LBR\3", 4)) return true;
  // The initial-message optimization sends a whole-root offer for <=32 records.
  // Its answer repeats the same bounds/count/items when equal, changing only
  // offer to answer. Comparing these bounded bytes avoids another tree walk.
  return request.size() >= 7 && request.size() == reply.size() &&
    request.substr(0, 6) == std::string_view("LBR\3\1\0", 6) &&
    reply.substr(0, 6) == std::string_view("LBR\3\2\0", 6) &&
    uint8_t(request[6]) <= reconciliation::LEAF_IDS &&
    request.size() == 7 + size_t(uint8_t(request[6])) * 16 &&
    request.substr(6) == reply.substr(6);
}
}
