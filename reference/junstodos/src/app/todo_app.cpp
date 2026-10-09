#include "todo_app.h"

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <TargetConditionals.h>

#include "../engine/glue.h"
#include "../font/text.h"
#include "../sync/auth.h"
#include "../sync/mirror.h"
#include "../sync/net_platform.h"

#if TARGET_OS_SIMULATOR
static const char* kSyncUrl = "http://127.0.0.1:8787/sync";
#else
static const char* kSyncUrl = "http://192.168.1.41:8787/sync";  // dev Mac on LAN
#endif

using edn::M;
using edn::Value;
using edn::kw;
using edn::sym;

// layout constants (logical px)
static const float kListTop = 110.0f;
static const float kLineH = 40.0f;
static const int kMaxShown = 16;

static struct {
    store::Store* store = nullptr;
    engine::Db db;  // main thread only
    int added_count = 0;

    // sync worker
    std::thread worker;
    std::mutex mu;
    std::condition_variable cv;
    bool dirty = false;
    bool quit = false;
    std::string sync_status = "not synced yet";  // worker writes under mu

    // current frame's rendered order, for tap-to-toggle
    std::vector<Value> shown;
} app;

static void signal_sync(void) {
    std::lock_guard<std::mutex> lock(app.mu);
    app.dirty = true;
    app.cv.notify_one();
}

static void worker_loop(void) {
    auto me = jsync::identity_load_or_create(
        std::string(getenv("HOME")) + "/Documents/identity.key");
    auto transport = jsync::authed_transport(jsync::http_transport(kSyncUrl), me.pub, &me);
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(app.mu);
            app.cv.wait(lock, [] { return app.dirty || app.quit; });
            if (app.quit) return;
            app.dirty = false;
        }
        std::string status;
        try {
            auto st = jsync::mirror_up(app.store, transport, /*allow_wipe=*/true);
            char buf[96];
            snprintf(buf, sizeof buf, "synced (+%zu -%zu, %.0fms)",
                     st.pushed, st.deleted_remote, st.total_us / 1000.0);
            status = st.converged ? buf : "sync did not converge";
        } catch (const std::exception& e) {
            status = std::string("offline: ") + e.what();
        }
        std::lock_guard<std::mutex> lock(app.mu);
        app.sync_status = status;
    }
}

static void persist_and_sync(void) {
    if (glue::sync_to_store(app.db, app.store)) signal_sync();
}

static void add_todo(const std::string& text) {
    app.db = glue::transact(std::move(app.db),
        M{{glue::new_entity_id(), M{{kw("text"), text}, {kw("done"), false}}}});
    persist_and_sync();
}

static void toggle_done(const Value& entity) {
    Value cur = engine::query(app.db, M{{entity, M{{kw("done"), sym("?d")}}}});
    if (cur.as_map().empty()) return;  // no such todo (or no :done fact on it)
    bool done = cur.get(entity).get(kw("done")) == Value(true);
    app.db = glue::transact(std::move(app.db), M{{entity, M{{kw("done"), !done}}}});
    persist_and_sync();
}

void todo_app_init(void) {
    std::string dir = std::string(getenv("HOME")) + "/Documents/todos-db";
    app.store = store::open(dir);
    app.db = glue::load(app.store);
    if (app.db.eav.size() == 0) {
        add_todo("welcome to jun's todos");
        add_todo("tap an item to toggle it");
        add_todo("tap empty space to add one");
    }
    app.worker = std::thread(worker_loop);
    signal_sync();  // initial mirror (or offline no-op)
}

void todo_app_frame(void) {
    text_draw("Jun's Todos", 24, 40, 34, 0x1A1A2EFF);

    // every live todo, straight from the query engine (pattern parsed once)
    static const Value todos_pattern =
        edn::read_string("{?e {:text ?text, :done ?done, :updated-at ?ts}}");
    Value found = engine::query(app.db, todos_pattern);

    // collect + sort newest-first
    struct Row { int64_t ts; Value entity; std::string text; bool done; };
    std::vector<Row> rows;
    for (const auto& [e, am] : found.as_map()) {
        Value text = am.get(kw("text"));
        if (text.type() != edn::Type::String) continue;
        rows.push_back({am.get(kw("updated-at")).as_int(), e, text.as_string(),
                        am.get(kw("done")) == Value(true)});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.ts > b.ts; });
    if ((int)rows.size() > kMaxShown) rows.resize(kMaxShown);

    app.shown.clear();
    float y = kListTop;
    for (const auto& r : rows) {
        app.shown.push_back(r.entity);
        std::string line = (r.done ? "✓ " : "○ ") + r.text;
        text_draw(line.c_str(), 24, y, 22, r.done ? 0x8A8AA0FF : 0x1A1A2EFF);
        y += kLineH;
    }

    std::string status;
    {
        std::lock_guard<std::mutex> lock(app.mu);
        status = app.sync_status;
    }
    text_draw(status.c_str(), 24, y + 20, 14, 0x1A1A2E99);
}

void todo_app_touch(float x, float y) {
    (void)x;
    int idx = (int)((y - kListTop) / kLineH);
    if (y >= kListTop && idx >= 0 && idx < (int)app.shown.size()) {
        toggle_done(app.shown[idx]);
    } else {
        app.added_count++;
        add_todo("todo #" + std::to_string(app.added_count) + " (tapped in)");
    }
}

void todo_app_shutdown(void) {
    {
        std::lock_guard<std::mutex> lock(app.mu);
        app.quit = true;
        app.cv.notify_one();
    }
    if (app.worker.joinable()) app.worker.join();
    glue::sync_to_store(app.db, app.store);
    store::close(app.store);
    app.store = nullptr;
}
