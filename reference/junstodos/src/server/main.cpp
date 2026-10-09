// junstodos sync server: single-threaded event loop (standalone Asio, C++20
// coroutines) around the transport-agnostic sync_handle(). One store, one
// thread, no locks — parked connections are just suspended coroutines.
//
//   ./junstodos-server <port> <db-dir>
//
// HTTP handling is a deliberately minimal hand-parse of the one request
// shape our own client sends (POST with Content-Length). Grows a real
// parser + TLS + auth when the multi-user model lands.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <asio.hpp>

#include "../db/store.h"
#include "../sync/auth.h"
#include "../sync/server.h"
#include "../sync/watch.h"

using asio::ip::tcp;

// One store per namespace (= per user pubkey), directory-per-pubkey, lazily
// opened. Single-threaded event loop, so no locking anywhere.
static std::string g_root;
static uint8_t g_secret[32];
static std::unordered_map<std::string, store::Store*> g_namespaces;

static store::Store* namespace_store(const uint8_t ns[jsync::PUBKEY_SIZE]) {
    std::string key((const char*)ns, jsync::PUBKEY_SIZE);
    auto it = g_namespaces.find(key);
    if (it != g_namespaces.end()) return it->second;
    std::string dir = g_root + "/" + jsync::pubkey_hex(ns);
    auto* s = store::open(dir);
    g_namespaces.emplace(std::move(key), s);
    fprintf(stderr, "namespace opened: %s\n", jsync::pubkey_hex(ns).c_str());
    return s;
}

// ---- the poke hub ------------------------------------------------------------
// A parked /wait request IS the subscription: it registers a Waiter under each
// watched namespace and suspends on a timer. A committed OPS cancels the
// timers of everyone watching that namespace; timeout or disconnect cleans up
// identically because removal happens on the coroutine's way out, always.
// Single-threaded event loop => no locks anywhere.

struct Waiter {
    asio::steady_timer* timer = nullptr;
    std::vector<jsync::Pubkey> changed;
};

static std::unordered_map<std::string, std::vector<Waiter*>> g_watchers;

static void hub_register(const std::vector<jsync::Pubkey>& subs, Waiter* w) {
    for (const auto& k : subs) {
        g_watchers[std::string((const char*)k.data(), k.size())].push_back(w);
    }
}

static void hub_deregister(const std::vector<jsync::Pubkey>& subs, Waiter* w) {
    for (const auto& k : subs) {
        auto it = g_watchers.find(std::string((const char*)k.data(), k.size()));
        if (it == g_watchers.end()) continue;
        std::erase(it->second, w);
        if (it->second.empty()) g_watchers.erase(it);
    }
}

static void hub_notify(const uint8_t ns[jsync::PUBKEY_SIZE]) {
    auto it = g_watchers.find(std::string((const char*)ns, jsync::PUBKEY_SIZE));
    if (it == g_watchers.end()) return;
    jsync::Pubkey key;
    memcpy(key.data(), ns, key.size());
    for (Waiter* w : it->second) {
        if (std::find(w->changed.begin(), w->changed.end(), key) == w->changed.end()) {
            w->changed.push_back(key);
        }
        w->timer->cancel();  // wakes the parked coroutine
    }
}

static asio::awaitable<std::string> handle_wait(std::string_view body) {
    auto subs = jsync::decode_watch_body(body);
    Waiter w;
    if (!subs.empty()) {
        asio::steady_timer timer(co_await asio::this_coro::executor);
        timer.expires_after(std::chrono::seconds(jsync::WAIT_PARK_SECONDS));
        w.timer = &timer;
        hub_register(subs, &w);
        asio::error_code ec;  // poked => operation_aborted; timeout => success
        co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        hub_deregister(subs, &w);
    }
    co_return jsync::encode_watch_body(w.changed);
}

static void load_or_create_secret(const std::string& path) {
    if (FILE* f = fopen(path.c_str(), "rb")) {
        size_t n = fread(g_secret, 1, 32, f);
        fclose(f);
        if (n == 32) return;
        throw std::runtime_error("corrupt token secret file");
    }
    arc4random_buf(g_secret, 32);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f || fwrite(g_secret, 1, 32, f) != 32) {
        throw std::runtime_error("cannot write token secret file");
    }
    fclose(f);
}

struct HttpRequest {
    std::string path;
    std::string body;
};

// Reads one HTTP request (POST, Content-Length framed) from the socket.
static asio::awaitable<HttpRequest> read_request(tcp::socket& sock, std::string& buf) {
    // headers
    size_t hdr_end;
    while ((hdr_end = buf.find("\r\n\r\n")) == std::string::npos) {
        char tmp[8192];
        size_t n = co_await sock.async_read_some(asio::buffer(tmp), asio::use_awaitable);
        buf.append(tmp, n);
        if (buf.size() > 1 << 20) throw std::runtime_error("headers too large");
    }
    std::string headers = buf.substr(0, hdr_end);

    // request line: METHOD SP PATH SP VERSION
    size_t sp1 = headers.find(' ');
    size_t sp2 = headers.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        throw std::runtime_error("bad request line");
    }
    HttpRequest req;
    req.path = headers.substr(sp1 + 1, sp2 - sp1 - 1);

    // Content-Length (case-insensitive scan)
    size_t content_length = 0;
    {
        std::string lower = headers;
        for (auto& c : lower) c = (char)tolower((unsigned char)c);
        size_t p = lower.find("content-length:");
        if (p != std::string::npos) {
            content_length = strtoull(lower.c_str() + p + 15, nullptr, 10);
        }
        if (content_length > 64u << 20) throw std::runtime_error("body too large");
    }

    size_t total = hdr_end + 4 + content_length;
    while (buf.size() < total) {
        char tmp[8192];
        size_t n = co_await sock.async_read_some(asio::buffer(tmp), asio::use_awaitable);
        buf.append(tmp, n);
    }
    req.body = buf.substr(hdr_end + 4, content_length);
    buf.erase(0, total);  // keep any pipelined bytes for the next request
    co_return req;
}

static asio::awaitable<void> session(tcp::socket sock) {
    std::string buf;
    try {
        for (;;) {  // keep-alive loop
            HttpRequest req = co_await read_request(sock, buf);

            int code = 200;
            std::string body;
            auto t0 = std::chrono::steady_clock::now();
            char kind = '?';
            try {
                if (req.path == "/sync") {
                    uint8_t ns[jsync::PUBKEY_SIZE];
                    std::string_view inner =
                        jsync::envelope_open(req.body, g_secret, jsync::now_ms(), ns);
                    kind = "?NOSF"[inner.empty() ? 0 : (uint8_t)inner[0] <= 4 ? (uint8_t)inner[0] : 0];
                    body = jsync::token_mint(g_secret, jsync::now_ms()) +
                           jsync::sync_handle(namespace_store(ns), inner);
                    if (kind == 'O') hub_notify(ns);  // poke the watchers
                } else if (req.path == "/wait") {
                    kind = 'W';
                    body = co_await handle_wait(req.body);
                } else {
                    code = 404;
                    body = "not found";
                }
            } catch (const std::exception& e) {
                code = 400;
                body = e.what();
                fprintf(stderr, "request error: %s\n", e.what());
            }

            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now() - t0).count();
            fprintf(stderr, "req %c  %6lldus  in=%zuB out=%zuB\n",
                    kind, (long long)us, req.body.size(), body.size());

            std::string resp = "HTTP/1.1 " + std::to_string(code) +
                               (code == 200 ? " OK" : " Error") +
                               "\r\nContent-Type: application/octet-stream"
                               "\r\nContent-Length: " + std::to_string(body.size()) +
                               "\r\n\r\n" + body;
            co_await asio::async_write(sock, asio::buffer(resp), asio::use_awaitable);
        }
    } catch (...) {
        // disconnect or malformed request: drop the connection
    }
}

static asio::awaitable<void> listener(unsigned short port) {
    auto ex = co_await asio::this_coro::executor;
    tcp::acceptor acceptor(ex, {tcp::v4(), port});
    fprintf(stderr, "junstodos-server listening on :%u\n", port);
    for (;;) {
        tcp::socket sock = co_await acceptor.async_accept(asio::use_awaitable);
        asio::co_spawn(ex, session(std::move(sock)), asio::detached);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> <db-dir>\n", argv[0]);
        return 2;
    }
    unsigned short port = (unsigned short)atoi(argv[1]);
    g_root = argv[2];
    std::filesystem::create_directories(g_root);
    load_or_create_secret(g_root + "/token-secret");

    asio::io_context ctx(1);  // single-threaded by declaration
    asio::co_spawn(ctx, listener(port), asio::detached);
    ctx.run();
    return 0;
}
