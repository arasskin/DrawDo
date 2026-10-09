#include "datafriend/identity.h"
#include "datafriend/wire_codec.h"
#include "datafriend/attributes.h"
#include "monocypher-ed25519.h"
#include <cerrno>
#include <stdexcept>
#ifdef __APPLE__
#include <Security/Security.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace datafriend {
namespace {
struct seed_buffer {
  uint8_t data[32]{};
  ~seed_buffer() { crypto_wipe(data, sizeof data); }
};
void random_seed(uint8_t seed[32]) {
#ifdef __APPLE__
  if(SecRandomCopyBytes(kSecRandomDefault, 32, seed) != errSecSuccess)
    throw std::runtime_error("identity randomness unavailable");
#else
  const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if(fd < 0) throw std::runtime_error("identity randomness unavailable");
  size_t offset = 0;
  while(offset < 32) {
    auto n = read(fd, seed + offset, 32 - offset);
    if(n < 0 && errno == EINTR) continue;
    if(n <= 0) { close(fd); throw std::runtime_error("identity randomness unavailable"); }
    offset += size_t(n);
  }
  close(fd);
#endif
}
}
identity::identity(std::string_view alias, bool ephemeral) {
  if(!ephemeral && (alias.empty() || alias.size() > 128))
    throw std::invalid_argument("identity alias must be 1-128 ASCII characters");
  for(unsigned char c : alias)
    if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
      throw std::invalid_argument("invalid identity alias");
  seed_buffer seed;
  random_seed(seed.data);
  if(!ephemeral) identity_load_or_create(alias, seed.data);
  crypto_ed25519_key_pair(secret_.data(), public_.data(), seed.data);
}
identity::~identity() { crypto_wipe(secret_.data(), secret_.size()); }
std::array<uint8_t, 64> identity::sign_write(std::string_view packet) const {
  // Limit the signer to this identity's current Littlebear write envelopes;
  // callers cannot ask this key to sign other protocols.
  if(packet.size() > 8 * 1024 * 1024 - 64 || packet.size() < 13 ||
     packet.substr(0, 4) != std::string_view("LBS\5", 4))
    throw std::invalid_argument("invalid owner write envelope");
  reader r{packet.substr(4)};
  auto request=r.u32(),channel=r.u32();auto operation=r.u8();
  if(!request || !channel || (operation != 3 && operation != 6))
    throw std::invalid_argument("invalid owner write channel");
  auto key = r.blob();
  if(key.size() != public_.size() ||
     crypto_verify32(reinterpret_cast<const uint8_t*>(key.data()), public_.data()) != 0)
    throw std::invalid_argument("owner key does not match write namespace");
  if(r.blob() != attribute_schema) throw std::invalid_argument("invalid write schema");
  r.u64(); r.u64(); r.blob();
  for(int i = 0; i < 32; ++i) r.u8();
  r.u64(); r.end();
  std::array<uint8_t, 64> signature;
  crypto_ed25519_sign(signature.data(), secret_.data(),
    reinterpret_cast<const uint8_t*>(packet.data()), packet.size());
  return signature;
}
#if !defined(__APPLE__) && !defined(__ANDROID__)
void identity_load_or_create(std::string_view, uint8_t[32]) {
  throw std::runtime_error("secure identity storage is implemented on Apple and Android only");
}
#endif
}
