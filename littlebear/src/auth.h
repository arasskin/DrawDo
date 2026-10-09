#pragma once
#include "protocol.h"
#include <sodium.h>

// Cryptography and replay state only. The server decides when to issue a challenge
// and passes the verified write permission to sync; this module cannot touch storage.
namespace auth {
struct state {
  unsigned char challenge[32]{};
  uint64_t sequence = 0;
  bool issued = false, pushing = false;
};
inline std::string namespace_id(std::string_view public_key) {
  std::string name(16, '\0');
  if(crypto_generichash(reinterpret_cast<unsigned char*>(name.data()), name.size(),
       reinterpret_cast<const unsigned char*>(public_key.data()), public_key.size(), nullptr, 0))
    throw std::runtime_error("namespace digest failed");
  return name;
}
inline void attach_challenge(state* auth, std::string& reply) {
  randombytes_buf(auth->challenge, sizeof auth->challenge);
  auth->sequence = 0; auth->issued = true;
  reply[0] = char(uint8_t(reply[0]) | 0x80);
  reply.insert(17, reinterpret_cast<const char*>(auth->challenge), sizeof auth->challenge);
}
inline void verify(state* auth, const lb_sync::request& request) {
  if((request.op != 3 && request.op != 6) || !auth->issued || request.sequence != auth->sequence ||
     request.challenge.size() != sizeof auth->challenge || request.signature.size() != crypto_sign_BYTES ||
     request.public_key.size() != crypto_sign_PUBLICKEYBYTES ||
     sodium_memcmp(request.challenge.data(), auth->challenge, sizeof auth->challenge) != 0 ||
     crypto_sign_verify_detached(reinterpret_cast<const unsigned char*>(request.signature.data()),
       reinterpret_cast<const unsigned char*>(request.signed_bytes.data()), request.signed_bytes.size(),
       reinterpret_cast<const unsigned char*>(request.public_key.data())) != 0)
    throw std::invalid_argument("invalid owner proof");
  // Consume before storage, including stale/failed writes. Lost replies require
  // reconciliation, never resending the old signed mutation.
  if(auth->sequence == UINT64_MAX) auth->issued = false;
  else ++auth->sequence;
}
}
