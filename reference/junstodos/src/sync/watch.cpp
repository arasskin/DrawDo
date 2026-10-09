#include "watch.h"

#include <cstring>
#include <stdexcept>

#include "net_platform.h"

namespace jsync {

std::string encode_watch_body(const std::vector<Pubkey>& keys) {
    std::string out;
    uint32_t n = (uint32_t)keys.size();
    for (int i = 3; i >= 0; i--) out.push_back((char)((n >> (8 * i)) & 0xFF));
    for (const auto& k : keys) out.append((const char*)k.data(), k.size());
    return out;
}

std::vector<Pubkey> decode_watch_body(std::string_view body) {
    if (body.size() < 4) throw std::runtime_error("watch: short body");
    uint32_t n = 0;
    for (int i = 0; i < 4; i++) n = (n << 8) | (uint8_t)body[i];
    if (n > WAIT_MAX_SUBS || body.size() != 4 + (size_t)n * 32) {
        throw std::runtime_error("watch: malformed body");
    }
    std::vector<Pubkey> keys(n);
    for (uint32_t i = 0; i < n; i++) {
        memcpy(keys[i].data(), body.data() + 4 + (size_t)i * 32, 32);
    }
    return keys;
}

std::vector<Pubkey> wait_for_changes(const std::string& wait_url,
                                     const std::vector<Pubkey>& subs) {
    auto t = http_transport(wait_url, WAIT_HTTP_TIMEOUT_S);
    return decode_watch_body(t(encode_watch_body(subs)));
}

}  // namespace jsync
