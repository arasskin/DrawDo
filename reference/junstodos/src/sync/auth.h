#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "mirror.h"

// Identity + authentication for the multiuser sync protocol.
//
// Every app install generates an Ed25519 keypair; the pubkey IS the user id
// and names the server-side namespace. Writes (OPS) must be signed by the
// namespace's key and carry a fresh server-issued time token (so no client
// clock is ever trusted); reads are open by design — permission checks
// happen in the app.
//
// Envelope (fixed 137-byte header + inner request):
//   [u8 ver=1][32B namespace pubkey][u64 token ts (BE)][32B token mac][64B sig][inner]
//   sig = ed25519 over (ver‖ns‖token‖inner); sig and token are zeroed on reads.
// Every server response is prefixed with a fresh 40-byte token:
//   [u64 ts (BE)][32B mac][inner response]
// token mac = keyed BLAKE2b(server secret, ts); tokens older than the replay
// window are rejected. Replay within the window is idempotent-or-healed by
// the mirror loop (see session notes / memory).

namespace jsync {

constexpr size_t PUBKEY_SIZE = 32;
constexpr size_t TOKEN_SIZE = 8 + 32;
constexpr size_t ENVELOPE_SIZE = 1 + PUBKEY_SIZE + TOKEN_SIZE + 64;
constexpr uint64_t REPLAY_WINDOW_MS = 5 * 60 * 1000;

struct Identity {
    uint8_t pub[PUBKEY_SIZE];
    uint8_t secret[64];
};

Identity identity_generate();
// load-or-create at path (hex text file). TODO: move to Keychain before release.
Identity identity_load_or_create(const std::string& path);
std::string pubkey_hex(const uint8_t pub[PUBKEY_SIZE]);
bool pubkey_from_hex(std::string_view hex, uint8_t out[PUBKEY_SIZE]);

uint64_t now_ms();

// ---- server side ----
std::string token_mint(const uint8_t secret[32], uint64_t now);
// Parse + verify an envelope. Verifies token freshness and signature iff the
// inner request is a write (OPS). Fills ns_out, returns the inner request
// view (into `req`). Throws std::runtime_error on any failure.
std::string_view envelope_open(std::string_view req, const uint8_t secret[32],
                               uint64_t now, uint8_t ns_out[PUBKEY_SIZE]);

// ---- client side ----
// Wraps a Transport with the auth envelope for namespace `ns`: prepends the
// header (signing writes with `signer`), strips + retains the token from
// every response. `signer` may be null for a read-only transport (e.g.
// reading a friend's namespace).
Transport authed_transport(Transport inner, const uint8_t ns[PUBKEY_SIZE],
                           const Identity* signer);

}  // namespace jsync
