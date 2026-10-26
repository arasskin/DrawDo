#pragma once
namespace lb_sync {
using wire::reader;
using wire::writer;
constexpr size_t max_frame = LB_MAX_FRAME, max_packet = LB_MAX_PACKET, max_records = LB_MAX_RECORDS;
inline uint64_t integer(std::string_view bytes) {
  return reader{bytes}.u64();
}
} // namespace lb_sync
