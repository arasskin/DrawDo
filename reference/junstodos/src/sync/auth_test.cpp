// Auth layer test: envelope roundtrip through a simulated server, signature
// enforcement on writes, open reads, token freshness window, tamper and
// wrong-key rejection, identity persistence.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "auth.h"
#include "protocol.h"

#define REQUIRE(cond, msg) do { \
    if (!(cond)) { printf("AUTH FAIL: %s\n", msg); return 1; } \
} while (0)

int auth_test_main(const char* base_dir) {
    using namespace jsync;
    std::filesystem::create_directories(base_dir);

    uint8_t secret[32];
    for (int i = 0; i < 32; i++) secret[i] = (uint8_t)(i * 7 + 3);

    Identity alice = identity_generate();
    Identity mallory = identity_generate();

    // simulated server: opens the envelope, echoes the inner request back
    uint8_t seen_ns[PUBKEY_SIZE];
    std::string captured_write;  // for the replay/tamper cases below
    Transport server = [&](std::string_view req) -> std::string {
        std::string_view inner = envelope_open(req, secret, now_ms(), seen_ns);
        if (!inner.empty() && (uint8_t)inner[0] == OPS) captured_write = std::string(req);
        return token_mint(secret, now_ms()) + std::string(inner);
    };

    auto t = authed_transport(server, alice.pub, &alice);

    // reads pass unsigned and deliver a token
    std::string stat_echo = t(encode_stat_req());
    REQUIRE((uint8_t)stat_echo[0] == STAT, "read roundtrip");
    REQUIRE(memcmp(seen_ns, alice.pub, PUBKEY_SIZE) == 0, "namespace = pubkey");

    // signed write accepted (token acquired by the read above)
    std::string ops = encode_ops_req({}, {});
    REQUIRE((uint8_t)t(ops)[0] == OPS, "signed write roundtrip");

    // write before any read (no token) must be refused client-side
    auto t2 = authed_transport(server, mallory.pub, &mallory);
    bool threw = false;
    try { t2(ops); } catch (const std::runtime_error&) { threw = true; }
    REQUIRE(threw, "write without token refused");

    // mallory signing into alice's namespace must be rejected by the server
    auto t3 = authed_transport(server, alice.pub, &mallory);
    t3(encode_stat_req());  // acquire token
    threw = false;
    try { t3(ops); } catch (const std::runtime_error&) { threw = true; }
    REQUIRE(threw, "wrong-key write rejected");

    // tampering with a captured signed write must break the signature
    REQUIRE(!captured_write.empty(), "captured a signed write");
    {
        std::string tampered = captured_write;
        tampered[tampered.size() - 1] ^= 1;
        uint8_t ns[PUBKEY_SIZE];
        threw = false;
        try { envelope_open(tampered, secret, now_ms(), ns); }
        catch (const std::runtime_error&) { threw = true; }
        REQUIRE(threw, "tampered write rejected");
    }

    // replay outside the window must be rejected; inside it is accepted
    {
        uint8_t ns[PUBKEY_SIZE];
        uint64_t later = now_ms() + REPLAY_WINDOW_MS + 60'000;
        threw = false;
        try { envelope_open(captured_write, secret, later, ns); }
        catch (const std::runtime_error&) { threw = true; }
        REQUIRE(threw, "replay after window rejected");
        envelope_open(captured_write, secret, now_ms(), ns);  // within: no throw
    }

    // identity persistence roundtrip
    std::string keyfile = std::string(base_dir) + "/identity.key";
    std::filesystem::remove(keyfile);
    Identity a = identity_load_or_create(keyfile);
    Identity b = identity_load_or_create(keyfile);
    REQUIRE(memcmp(a.pub, b.pub, PUBKEY_SIZE) == 0 &&
            memcmp(a.secret, b.secret, 64) == 0, "identity persists");

    printf("OK: auth test passed\n");
    return 0;
}
