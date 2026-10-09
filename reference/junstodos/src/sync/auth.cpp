#include "auth.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "protocol.h"
#include "monocypher/monocypher.h"
#include "monocypher/monocypher-ed25519.h"

namespace jsync {

// ---- identity ----------------------------------------------------------------

Identity identity_generate() {
    Identity id;
    uint8_t seed[32];
    arc4random_buf(seed, sizeof seed);  // CSPRNG on Apple + Android bionic
    crypto_ed25519_key_pair(id.secret, id.pub, seed);  // wipes seed
    return id;
}

static const char* kHex = "0123456789abcdef";

static std::string to_hex(const uint8_t* p, size_t n) {
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; i++) {
        s[2 * i] = kHex[p[i] >> 4];
        s[2 * i + 1] = kHex[p[i] & 15];
    }
    return s;
}

static bool from_hex(std::string_view s, uint8_t* out, size_t n) {
    if (s.size() != n * 2) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i < n; i++) {
        int hi = nib(s[2 * i]), lo = nib(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

std::string pubkey_hex(const uint8_t pub[PUBKEY_SIZE]) { return to_hex(pub, PUBKEY_SIZE); }

bool pubkey_from_hex(std::string_view hex, uint8_t out[PUBKEY_SIZE]) {
    return from_hex(hex, out, PUBKEY_SIZE);
}

Identity identity_load_or_create(const std::string& path) {
    Identity id;
    if (FILE* f = fopen(path.c_str(), "r")) {
        char buf[256] = {0};
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        std::string_view sv(buf, n);
        while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r')) sv.remove_suffix(1);
        if (sv.size() == (64 + 32) * 2 &&
            from_hex(sv.substr(0, 128), id.secret, 64) &&
            from_hex(sv.substr(128), id.pub, 32)) {
            return id;
        }
        throw std::runtime_error("auth: corrupt identity file: " + path);
    }
    id = identity_generate();
    if (FILE* f = fopen(path.c_str(), "w")) {
        std::string hex = to_hex(id.secret, 64) + to_hex(id.pub, 32);
        fputs(hex.c_str(), f);
        fclose(f);
    } else {
        throw std::runtime_error("auth: cannot write identity file: " + path);
    }
    return id;
}

uint64_t now_ms() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// ---- tokens -------------------------------------------------------------------

static void be64(uint8_t* p, uint64_t x) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(x >> (8 * (7 - i)));
}

static uint64_t rd_be64(const uint8_t* p) {
    uint64_t x = 0;
    for (int i = 0; i < 8; i++) x = (x << 8) | p[i];
    return x;
}

std::string token_mint(const uint8_t secret[32], uint64_t now) {
    std::string t(TOKEN_SIZE, '\0');
    be64((uint8_t*)t.data(), now);
    crypto_blake2b_keyed((uint8_t*)t.data() + 8, 32, secret, 32,
                         (const uint8_t*)t.data(), 8);
    return t;
}

static bool token_check(const uint8_t* token, const uint8_t secret[32], uint64_t now) {
    uint8_t mac[32];
    crypto_blake2b_keyed(mac, 32, secret, 32, token, 8);
    if (crypto_verify32(mac, token + 8) != 0) return false;  // constant-time
    uint64_t ts = rd_be64(token);
    uint64_t diff = now > ts ? now - ts : ts - now;
    return diff <= REPLAY_WINDOW_MS;
}

// ---- envelope -----------------------------------------------------------------

std::string_view envelope_open(std::string_view req, const uint8_t secret[32],
                               uint64_t now, uint8_t ns_out[PUBKEY_SIZE]) {
    if (req.size() < ENVELOPE_SIZE + 1) throw std::runtime_error("auth: short request");
    const uint8_t* p = (const uint8_t*)req.data();
    if (p[0] != 1) throw std::runtime_error("auth: bad envelope version");
    memcpy(ns_out, p + 1, PUBKEY_SIZE);
    const uint8_t* token = p + 1 + PUBKEY_SIZE;
    const uint8_t* sig = token + TOKEN_SIZE;
    std::string_view inner = req.substr(ENVELOPE_SIZE);

    if ((uint8_t)inner[0] == OPS) {  // writes: fresh token + owner signature
        if (!token_check(token, secret, now)) {
            throw std::runtime_error("auth: stale or invalid token (server time " +
                                     std::to_string(now) + ")");
        }
        std::string msg;
        msg.reserve(1 + PUBKEY_SIZE + TOKEN_SIZE + inner.size());
        msg.append(req.data(), 1 + PUBKEY_SIZE + TOKEN_SIZE);
        msg.append(inner);
        if (crypto_ed25519_check(sig, ns_out, (const uint8_t*)msg.data(), msg.size()) != 0) {
            throw std::runtime_error("auth: bad signature for write");
        }
    }
    return inner;
}

// ---- client transport decorator ------------------------------------------------

Transport authed_transport(Transport inner, const uint8_t ns[PUBKEY_SIZE],
                           const Identity* signer) {
    struct State {
        Transport inner;
        uint8_t ns[PUBKEY_SIZE];
        Identity signer;
        bool can_sign;
        std::string token;  // latest server-issued token
    };
    auto st = std::make_shared<State>();
    st->inner = std::move(inner);
    memcpy(st->ns, ns, PUBKEY_SIZE);
    st->can_sign = (signer != nullptr);
    if (signer) st->signer = *signer;

    return [st](std::string_view request) -> std::string {
        bool is_write = !request.empty() && (uint8_t)request[0] == OPS;
        if (is_write && !st->can_sign) {
            throw std::runtime_error("auth: read-only transport cannot send writes");
        }
        if (is_write && st->token.size() != TOKEN_SIZE) {
            throw std::runtime_error("auth: no token yet (a read must precede writes)");
        }

        std::string env;
        env.reserve(ENVELOPE_SIZE + request.size());
        env.push_back((char)1);
        env.append((const char*)st->ns, PUBKEY_SIZE);
        env.append(is_write ? st->token : std::string(TOKEN_SIZE, '\0'));
        if (is_write) {
            std::string msg = env + std::string(request);  // ver‖ns‖token‖inner
            uint8_t sig[64];
            crypto_ed25519_sign(sig, st->signer.secret, (const uint8_t*)msg.data(), msg.size());
            env.append((const char*)sig, 64);
        } else {
            env.append(64, '\0');
        }
        env.append(request);

        std::string resp = st->inner(env);
        if (resp.size() < TOKEN_SIZE) throw std::runtime_error("auth: short response");
        st->token = resp.substr(0, TOKEN_SIZE);  // refresh from every response
        return resp.substr(TOKEN_SIZE);
    };
}

}  // namespace jsync
