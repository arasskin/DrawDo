#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The poke channel (design: poke-and-pull long-polling). A client POSTs the
// list of namespaces it watches to /wait; the server parks the request until
// one of them changes (responding with the changed namespaces) or ~45s pass
// (responding with an empty list). The parked request IS the subscription —
// no registry, no cleanup, no server state beyond it.
//
// Pokes are pure latency hints: correctness comes from the reconcile at the
// top of every client cycle, never from the poke channel.
//
// Body format both directions: [u32 count] count x [32B pubkey]. No envelope,
// no auth — watching is a read, and reads are open by design.

namespace jsync {

using Pubkey = std::array<uint8_t, 32>;

constexpr int WAIT_PARK_SECONDS = 45;      // server-side park ceiling
constexpr int WAIT_HTTP_TIMEOUT_S = 55;    // client HTTP timeout (> park)
constexpr size_t WAIT_MAX_SUBS = 256;

std::string encode_watch_body(const std::vector<Pubkey>& keys);
std::vector<Pubkey> decode_watch_body(std::string_view body);  // throws on malformed

// Client helper: blocks up to ~WAIT_HTTP_TIMEOUT_S posting to wait_url.
// Returns the namespaces that changed (empty = timeout, just cycle again).
// Background threads only, like all transports.
std::vector<Pubkey> wait_for_changes(const std::string& wait_url,
                                     const std::vector<Pubkey>& subs);

}  // namespace jsync
