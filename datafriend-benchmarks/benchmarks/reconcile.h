// Custom experimental range reconciliation. Not upstream Negentropy wire format.
#pragma once
#include "support.h"
#include <span>

namespace experiment::reconciliation {
constexpr size_t leaf_limit = 32, fanout = 16, packet_limit = 64 * 1024;
enum class kind : uint8_t { summary = 0, proposal = 1, final_list = 2 };
struct frame {
  kind type = kind::summary;
  key_range range;
  uint64_t hash = 0;
  std::vector<record> items;
};
using bytes = std::vector<uint8_t>;

inline void append(bytes& out, uint64_t value, size_t width) {
  for (size_t i = width; i; --i) out.push_back(uint8_t(value >> (8*(i-1))));
}
inline bytes encode_frame(const frame& f) {
  bytes out;
  append(out, uint8_t(f.type), 1);
  append(out, f.range.low, 8);
  append(out, f.range.high.has_value(), 1);
  append(out, f.range.high.value_or(0), 8);
  if (f.type == kind::summary) append(out, f.hash, 8);
  else {
    require(f.items.size() <= leaf_limit, "unbounded item frame");
    append(out, f.items.size(), 4);
    for (const auto& e : f.items) { append(out, e.key, 8); append(out, e.hash, 8); }
  }
  return out;
}
// Multiple packets in one direction are one protocol turn, not extra round trips.
inline std::vector<bytes> encode(const std::vector<frame>& frames) {
  std::vector<bytes> packets;
  bytes packet{1, 0, 0, 0, 0}; // version + frame count
  uint32_t count = 0;
  auto flush = [&] {
    for (unsigned i = 0; i < 4; ++i) packet[i+1] = uint8_t(count >> (24-8*i));
    packets.push_back(std::move(packet)); packet = {1, 0, 0, 0, 0}; count = 0;
  };
  for (const auto& f : frames) {
    auto encoded = encode_frame(f);
    if (packet.size()+encoded.size() > packet_limit) flush();
    packet.insert(packet.end(), encoded.begin(), encoded.end()); ++count;
  }
  if (count || packets.empty()) flush();
  return packets;
}
inline std::vector<frame> decode(std::span<const uint8_t> packet) {
  require(packet.size() <= packet_limit, "oversized packet");
  size_t pos = 0;
  auto take = [&](size_t n) {
    require(n <= packet.size()-pos, "truncated frame");
    uint64_t v = 0;
    while (n--) v = (v << 8) | packet[pos++];
    return v;
  };
  require(take(1) == 1, "unknown version");
  const auto count = take(4);
  require(count <= packet.size()/18, "invalid frame count");
  std::vector<frame> frames;
  for (uint64_t i = 0; i < count; ++i) {
    frame f; const auto type = take(1);
    require(type <= 2, "unknown frame type"); f.type = kind(type);
    f.range.low = take(8);
    const auto has_high = take(1); require(has_high <= 1, "invalid bound tag");
    const auto high = take(8); if (has_high) f.range.high = high;
    require(!f.range.empty(), "empty frame range");
    if (f.type == kind::summary) f.hash = take(8);
    else {
      const auto size = take(4); require(size <= leaf_limit, "oversized item list");
      for (size_t j = 0; j < size; ++j) {
        const auto k = take(8), hash = take(8);
        require(f.range.contains(k) && (j == 0 || f.items.back().key < k), "unsorted/out-of-range item");
        f.items.push_back({k, 0, hash});
      }
    }
    frames.push_back(std::move(f));
  }
  require(pos == packet.size(), "trailing packet bytes");
  return frames;
}

struct work_stats { size_t hash_queries = 0, split_calls = 0, list_calls = 0; };
template<class Index> class endpoint {
 public:
  Index& index;
  std::set<uint64_t> differences;
  work_stats work;
  explicit endpoint(Index& i) : index(i) {}

  void receive(const frame& f, std::vector<frame>& out) {
    if (f.type == kind::summary) {
      ++work.hash_queries;
      if (index.range_hash(f.range) != f.hash) refine(f.range, out);
    } else if (f.type == kind::proposal) {
      // The remote list covers the ENTIRE range. Our side can be arbitrarily
      // larger. Enumerate in bounded pages and send every page with exact bounds.
      key_range remaining = f.range;
      for (;;) {
        ++work.list_calls;
        auto own = index.list(remaining, leaf_limit+1);
        key_range page = remaining;
        const bool more = own.size() > leaf_limit;
        if (more) { page.high = own.back().key; own.pop_back(); }
        compare(page, own, f.items);
        out.push_back({kind::final_list, page, 0, std::move(own)});
        if (!more) break;
        remaining.low = *page.high;
      }
    } else {
      ++work.list_calls;
      const auto own = index.list(f.range, leaf_limit+1);
      require(own.size() <= leaf_limit, "final response exceeds original proposal range");
      compare(f.range, own, f.items);
    }
  }
 private:
  void compare(key_range r, const std::vector<record>& own, const std::vector<record>& remote) {
    size_t i = 0, j = 0;
    while (j < remote.size() && remote[j].key < r.low) ++j;
    while (i < own.size() || (j < remote.size() && r.contains(remote[j].key))) {
      if (j == remote.size() || !r.contains(remote[j].key) ||
          (i < own.size() && own[i].key < remote[j].key)) differences.insert(own[i++].key);
      else if (i == own.size() || remote[j].key < own[i].key) differences.insert(remote[j++].key);
      else {
        if (own[i].hash != remote[j].hash) differences.insert(own[i].key);
        ++i; ++j;
      }
    }
  }
  void refine(key_range r, std::vector<frame>& out) {
    ++work.list_calls;
    auto small = index.list(r, leaf_limit+1);
    if (small.size() <= leaf_limit) {
      out.push_back({kind::proposal, r, 0, std::move(small)}); return;
    }
    ++work.split_calls;
    const auto cuts = index.cuts(r, fanout);
    require(!cuts.empty(), "split made no progress");
    auto emit = [&](key_range part) {
      require(!part.empty(), "non-progressing cut");
      ++work.hash_queries;
      out.push_back({kind::summary, part, index.range_hash(part), {}});
    };
    auto low = r.low;
    for (const auto cut : cuts) {
      require(cut > low && (!r.high || cut < *r.high), "invalid cut");
      emit({low, cut}); low = cut;
    }
    emit({low, r.high});
  }
};

struct result {
  size_t turns = 0, packets = 0, wire_bytes = 0;
  size_t client_bytes = 0, server_bytes = 0;
  work_stats client_work, server_work;
  std::set<uint64_t> differences;
};

// Transport is actual encode/decode in memory. No sockets, sleeps, payload I/O,
// or concurrent mutation. Packet count != sequential turns; both are reported.
template<class Client, class Server> result run(Client& client, Server& server) {
  endpoint<Client> a(client); endpoint<Server> b(server);
  result stats;
  std::vector<bytes> packets = encode({{kind::summary, {}, client.range_hash({}), {}}});
  ++a.work.hash_queries;
  bool to_server = true;
  for (;;) {
    require(++stats.turns <= 256, "reconciliation did not terminate");
    std::vector<frame> response;
    bool received_frames = false;
    for (const auto& packet : packets) {
      ++stats.packets; stats.wire_bytes += packet.size();
      (to_server ? stats.client_bytes : stats.server_bytes) += packet.size();
      for (const auto& f : decode(packet)) {
        received_frames = true;
        if (to_server) b.receive(f, response); else a.receive(f, response);
      }
    }
    if (!received_frames) break; // explicit empty completion message
    packets = encode(response); to_server = !to_server;
  }
  require(a.differences == b.differences, "peers disagree about differences");
  stats.differences = std::move(a.differences);
  stats.client_work = a.work; stats.server_work = b.work;
  return stats;
}
} // namespace experiment::reconciliation
