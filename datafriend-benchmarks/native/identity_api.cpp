#include "identity.h"
#include "datafriend/c_api.h"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace {
void copy(df_buffer* out, const void* data, size_t size) {
  auto* p = static_cast<uint8_t*>(std::malloc(size));
  if(!p) throw std::bad_alloc{};
  std::memcpy(p, data, size);
  *out = {p, size};
}
void fail(df_buffer* out, const char* message) noexcept {
  try { copy(out, message, std::strlen(message)); } catch(...) {}
}
}
extern "C" {
void* df_identity_open(const uint8_t* alias, size_t size, int ephemeral, df_buffer* out) {
  *out = {};
  try {
    if((!alias && size) || size > 128 || (ephemeral != 0 && ephemeral != 1))
      throw std::invalid_argument("invalid identity options");
    auto key = std::make_unique<datafriend::identity>(
      std::string_view(alias ? reinterpret_cast<const char*>(alias) : "", size), ephemeral != 0);
    copy(out, key->public_key().data(), key->public_key().size());
    return key.release();
  } catch(const std::exception& e) { fail(out, e.what()); return nullptr; }
}
int df_identity_sign(void* handle, const uint8_t* message, size_t size, df_buffer* out) {
  *out = {};
  try {
    if(!handle || !message) throw std::invalid_argument("closed identity or invalid message");
    auto signature = static_cast<datafriend::identity*>(handle)->sign_write(
      {reinterpret_cast<const char*>(message), size});
    copy(out, signature.data(), signature.size());
    return 0;
  } catch(const std::exception& e) { fail(out, e.what()); return 1; }
}
void df_identity_close(void* handle) { delete static_cast<datafriend::identity*>(handle); }
}
