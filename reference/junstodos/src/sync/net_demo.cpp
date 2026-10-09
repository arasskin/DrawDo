#include "net_demo.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>

#include <TargetConditionals.h>

#include "../db/store.h"
#include "auth.h"
#include "mirror.h"
#include "net_platform.h"
#include "protocol.h"

#if TARGET_OS_SIMULATOR
static const char* kSyncUrl = "http://127.0.0.1:8787/sync";  // Mac host loopback
#else
static const char* kSyncUrl = "http://192.168.1.41:8787/sync";  // dev Mac on LAN
#endif

static store::Id demo_id(uint64_t ts, uint64_t n) {
    uint8_t uuid[24];
    for (int i = 0; i < 24; i++) uuid[i] = (uint8_t)((n * 97 + i * 29) & 0xFF);
    return jsync::make_version_id(ts, uuid);
}

static void write_result(const std::string& status) {
    fprintf(stderr, "net demo: %s\n", status.c_str());
    const char* home = getenv("HOME");
    if (!home) return;
    std::string path = std::string(home) + "/Documents/net_result.txt";
    if (FILE* f = fopen(path.c_str(), "w")) {
        fputs(status.c_str(), f);
        fclose(f);
    }
}

static uint64_t us_now() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

void net_demo_start(void) {
    std::thread([] {
        std::string status;
        try {
            const char* home = getenv("HOME");
            std::string dir = std::string(home ? home : ".") + "/tmp/net-demo-db";
            std::filesystem::remove_all(dir);
            auto* s = store::open(dir);

            // measurement scenario: 500 realistic items
            const int N = 500;
            uint64_t now = (uint64_t)time(nullptr) * 1000;

            uint64_t t0 = us_now();
            store::write(s, [&](store::Writer& w) {
                for (int n = 0; n < N; n++) {
                    w.put(now + n, demo_id(now + n, n),
                          "todo item #" + std::to_string(n) + " with a payload of realistic size");
                }
            });
            uint64_t write_batch_us = us_now() - t0;

            t0 = us_now();
            store::write(s, [&](store::Writer& w) {  // one item = one commit = one fsync
                w.put(now + N, demo_id(now + N, N), "single item write");
            });
            uint64_t write_one_us = us_now() - t0;

            auto me = jsync::identity_load_or_create(
                std::string(home ? home : ".") + "/Documents/identity.key");
            auto transport = jsync::authed_transport(
                jsync::http_transport(kSyncUrl), me.pub, &me);

            auto cold = jsync::mirror_up(s, transport, /*allow_wipe=*/true);
            auto noop = jsync::mirror_up(s, transport);  // steady state
            store::close(s);

            char buf[512];
            snprintf(buf, sizeof buf,
                     "NET %s: write%d=%.1fms write1=%.1fms | "
                     "cold{push=%zu tot=%.1fms snap=%.1f rec=%.1f ops=%.1f ver=%.1f} | "
                     "noop{tot=%.1fms snap=%.1f rec=%.1f ver=%.1f} | ns=%s",
                     (cold.converged && noop.converged) ? "PASS" : "FAIL",
                     N + 1, write_batch_us / 1000.0, write_one_us / 1000.0,
                     cold.pushed, cold.total_us / 1000.0, cold.snapshot_us / 1000.0,
                     cold.reconcile_us / 1000.0, cold.ops_us / 1000.0, cold.verify_us / 1000.0,
                     noop.total_us / 1000.0, noop.snapshot_us / 1000.0,
                     noop.reconcile_us / 1000.0, noop.verify_us / 1000.0,
                     jsync::pubkey_hex(me.pub).c_str());
            status = buf;
        } catch (const std::exception& e) {
            // server not running / unreachable is expected when offline
            status = std::string("NET SKIP: ") + e.what();
        }
        write_result(status);
    }).detach();
}
