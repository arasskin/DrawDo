#pragma once
#include <array>
#include <string_view>
#include <cstdint>

namespace datafriend {
// Secret state has no serialization/export API. Platform storage never uses EAV.
class identity {
  std::array<uint8_t, 64> secret_{};
  std::array<uint8_t, 32> public_{};
public:
  identity(std::string_view alias, bool ephemeral);
  ~identity();
  identity(const identity&) = delete;
  identity& operator=(const identity&) = delete;
  const auto& public_key() const { return public_; }
  std::array<uint8_t, 64> sign_write(std::string_view packet) const;
};
// Input is a freshly generated candidate. Replace with existing seed if present,
// or durably protect it before returning. Failures MUST NOT rotate an identity.
void identity_load_or_create(std::string_view alias, uint8_t seed[32]);
}
