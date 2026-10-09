// Fixed-workload comparison, not a production SQLite Datafriend backend.
// Both SQL layouts use Datafriend's persistent result trees and reconciliation.
#include "datafriend/engine.h"
#include <ankerl/unordered_dense.h>
#include <sqlite3.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <numeric>
#include <thread>
#include <unistd.h>

using namespace datafriend;
using bench_clock = std::chrono::steady_clock;
using dense_keys = ankerl::unordered_dense::set<uint64_t>;
static double micros(bench_clock::duration d) {
  return std::chrono::duration<double, std::micro>(d).count();
}
static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

// Same packaged-task/future handoff as the native engine; one owner, no readers.
class serial_executor {
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::function<void()>> jobs;
  bool stop = false;
  std::thread owner;
 public:
  serial_executor() : owner([this] {
    for (;;) {
      std::function<void()> job;
      {
        std::unique_lock lock(mutex);
        wake.wait(lock, [&] { return stop || !jobs.empty(); });
        if (jobs.empty()) return;
        job = std::move(jobs.front()); jobs.pop_front();
      }
      job();
    }
  }) {}
  ~serial_executor() {
    { std::lock_guard lock(mutex); stop = true; }
    wake.notify_one(); owner.join();
  }
  template<class F> auto call(F&& f) -> std::invoke_result_t<F> {
    using result_type = std::invoke_result_t<F>;
    auto task = std::make_shared<std::packaged_task<result_type()>>(std::forward<F>(f));
    auto result = task->get_future();
    { std::lock_guard lock(mutex); jobs.emplace_back([task] { (*task)(); }); }
    wake.notify_one(); return result.get();
  }
};

struct sql_connection {
  sqlite3* db = nullptr;
  explicit sql_connection(const std::string& path) {
    int rc = sqlite3_open_v2(path.c_str(), &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
    if (rc != SQLITE_OK) {
      std::string error = db ? sqlite3_errmsg(db) : "SQLite open failed";
      sqlite3_close(db); db = nullptr; throw std::runtime_error(error);
    }
  }
  ~sql_connection() { sqlite3_close(db); }
  void exec(const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db));
  }
};
struct statement {
  sqlite3_stmt* stmt = nullptr;
  statement(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db));
  }
  ~statement() { sqlite3_finalize(stmt); }
  void check(int rc) {
    if (rc != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(stmt)));
  }
  void reset() { check(sqlite3_reset(stmt)); check(sqlite3_clear_bindings(stmt)); }
  void text(int index, const std::string& value) {
    check(sqlite3_bind_text(stmt, index, value.data(), int(value.size()), SQLITE_TRANSIENT));
  }
  void integer(int index, uint64_t value) { check(sqlite3_bind_int64(stmt, index, value)); }
  bool row() {
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE)
      throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(stmt)));
    return rc == SQLITE_ROW;
  }
  void done() { require(!row(), "unexpected SQLite result row"); }
  std::string column(int index) {
    auto p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, index));
    return p ? std::string(p, sqlite3_column_bytes(stmt, index)) : std::string{};
  }
};

static constexpr const char* eav_select =
    "SELECT d.entity,d.value,t.value FROM triples g "
    "JOIN triples d ON d.entity=g.entity AND d.attribute='done' "
    "JOIN triples t ON t.entity=g.entity AND t.attribute='title' "
    "WHERE g.attribute='group' AND g.value=?";
static constexpr const char* tasks_select =
    "SELECT entity,done,title FROM tasks WHERE group_id=?";

// Benchmark-only mutable nested map. SQL and native retain live result handles.
class sql_result {
  struct state {
    ankerl::unordered_dense::map<uint32_t,ankerl::unordered_dense::map<uint32_t,value_ref>> rows;
    std::shared_ptr<view_scope> scope;
    explicit state(std::shared_ptr<view_scope> s):scope(std::move(s)) {}
  };
  std::shared_ptr<state> data;
 public:
  struct entry { uint64_t key; value_ref value; };
  explicit sql_result(std::shared_ptr<view_scope> s):data(std::make_shared<state>(std::move(s))) {}
  sql_result(const query_result& r):sql_result(r.scope()) {
    r.for_each([&](const auto& e){ set(e.key,e.value,0); });
  }
  auto scope() const { return data->scope; }
  auto find(uint64_t k) const -> std::optional<entry> {
    auto row=data->rows.find(uint32_t(k>>32)); if(row==data->rows.end()) return {};
    auto a=row->second.find(uint32_t(k)); if(a==row->second.end()) return {};
    return entry{k,a->second};
  }
  auto lookup(uint32_t e,std::string_view a) const -> std::optional<entry> {
    auto id=scope()->find(a); return id?find(pair_key(e,*id)):std::optional<entry>{};
  }
  triple decode(const entry& e) const { return {e.key>>32,scope()->name_of(uint32_t(e.key)),e.value->value}; }
  sql_result set(uint64_t k,value_ref v,uint64_t) { data->rows[uint32_t(k>>32)][uint32_t(k)]=std::move(v); return *this; }
  sql_result remove(uint64_t k) {
    auto row=data->rows.find(uint32_t(k>>32)); if(row!=data->rows.end()) {
      row->second.erase(uint32_t(k)); if(row->second.empty()) data->rows.erase(row);
    } return *this;
  }
  template<class F> void for_each(F&& f) const {
    for(const auto& [e,row]:data->rows) for(const auto& [a,v]:row) f(entry{pair_key(e,a),v});
  }
  uint64_t size() const { uint64_t n=0; for(const auto& [e,row]:data->rows) n+=row.size(); return n; }
  uint64_t fingerprint() const { uint64_t h=0; for_each([&](auto e){h^=triple_hash(e.key,*e.value);}); return h; }
  bool shares_with(const sql_result& other) const { return data==other.data; }
};

class sqlite_store {
  sql_connection connection;
  std::unique_ptr<statement> select, update, begin, commit;
  std::shared_ptr<view_scope> scope=std::make_shared<view_scope>("benchmark");
  uint32_t done_id=0,title_id=0;
  struct projected { uint32_t entity,attribute; bytes value; };
  std::vector<sql_result> roots;
  std::vector<size_t> handles;
  bool buffered, buffer_facts;
 public:
  struct timings {
    double query_us = 0, sql_read_us = 0, view_us = 0, buffer_release_us = 0;
    uint64_t rows = 0;
  } profile;
  sqlite_store(const std::string& path, bool eav, size_t entities, size_t extra, std::string_view mode)
      : connection(path), buffered(mode != "stream"), buffer_facts(mode == "facts") {
    done_id=scope->intern("done"); scope->intern("group"); title_id=scope->intern("title");
    connection.exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=OFF; "
                    "PRAGMA cache_size=-65536; PRAGMA wal_autocheckpoint=1000;");
    if (eav) {
      connection.exec("CREATE TABLE triples(entity INTEGER NOT NULL, attribute TEXT NOT NULL, "
          "value TEXT NOT NULL, PRIMARY KEY(entity,attribute)) WITHOUT ROWID; "
          "CREATE INDEX ave ON triples(attribute,value,entity);");
    } else {
      connection.exec("CREATE TABLE tasks(entity INTEGER PRIMARY KEY,group_id TEXT NOT NULL, "
          "done TEXT NOT NULL,title TEXT NOT NULL); CREATE INDEX task_group ON tasks(group_id);");
    }
    connection.exec("BEGIN");
    {
      statement insert(connection.db, eav ? "INSERT INTO triples VALUES(?,?,?)" :
                                            "INSERT INTO tasks VALUES(?,?,?,?)");
      for (uint64_t id = 1; id <= entities; ++id) {
        if (eav) {
          for (const auto& [attribute, value] : entity_record{
                   {"group", std::to_string(id % 100)}, {"done", "false"}, {"title", "task"}}) {
            insert.reset(); insert.integer(1, id); insert.text(2, attribute);
            insert.text(3, value); insert.done();
          }
        } else {
          insert.reset(); insert.integer(1, id); insert.text(2, std::to_string(id % 100));
          insert.text(3, "false"); insert.text(4, "task"); insert.done();
        }
      }
    }
    connection.exec("COMMIT; ANALYZE; PRAGMA wal_checkpoint(TRUNCATE);");
    const char* query_sql = eav ? eav_select : tasks_select;
    select = std::make_unique<statement>(connection.db, query_sql);
    update = std::make_unique<statement>(connection.db, eav ?
        "UPDATE triples SET value=? WHERE entity=? AND attribute='done'" :
        "UPDATE tasks SET done=? WHERE entity=?");
    begin = std::make_unique<statement>(connection.db, "BEGIN IMMEDIATE");
    commit = std::make_unique<statement>(connection.db, "COMMIT");
    std::cerr << "sqlite_version=" << sqlite3_libversion() << " source=" << sqlite3_sourceid() << '\n';
    for (const char* pragma : {"journal_mode", "synchronous", "cache_size", "wal_autocheckpoint", "page_size"}) {
      statement info(connection.db, (std::string("PRAGMA ") + pragma).c_str());
      require(info.row(), "missing SQLite configuration");
      std::cerr << pragma << '=' << info.column(0) << '\n';
      if (std::string(pragma) == "journal_mode") require(info.column(0) == "wal", "WAL unavailable");
      if (std::string(pragma) == "synchronous") require(info.column(0) == "0", "sync not disabled");
    }
    statement plan(connection.db, (std::string("EXPLAIN QUERY PLAN ") + query_sql).c_str());
    plan.text(1, "1");
    while (plan.row()) std::cerr << "plan=" << plan.column(3) << '\n';
    for (size_t group = 1; group <= extra + 1; ++group) roots.push_back(evaluate(group, sql_result{scope}));
    // Ten identical handles share group 1; the remaining handles are distinct.
    handles.assign(10, 0);
    for (size_t i = 1; i <= extra; ++i) handles.push_back(i);
  }
  template<class Enumerate>
  sql_result materialize(const sql_result& previous, Enumerate&& enumerate) {
    sql_result result = previous;
    dense_keys seen;
    auto emit = [&](uint32_t entity, uint32_t attribute, const bytes& bytes) {
      const auto key=pair_key(entity,attribute); seen.insert(key);
      if(auto old=result.find(key); old && old->value->value==bytes) return;
      auto value=std::make_shared<const boxed_value>(bytes);
      result=result.set(key,value,0);
    };
    enumerate(emit);
    std::vector<uint64_t> removed;
    previous.for_each([&](const sql_result::entry& entry) { if(!seen.contains(entry.key)) removed.push_back(entry.key); });
    for(auto key:removed) result.remove(key);
    return result;
  }
  sql_result evaluate(size_t group, const sql_result& previous) {
    if (!buffered) return materialize(previous, [&](auto&& emit) {
      select->reset(); select->text(1, std::to_string(group));
      while (select->row()) {
        auto id = uint64_t(sqlite3_column_int64(select->stmt, 0));
        emit(uint32_t(id), done_id, select->column(1));
        emit(uint32_t(id), title_id, select->column(2));
      }
    });
    // Diagnostic path: time whole loops, not each sqlite3_step/column/tree lookup.
    // Owned rows are essential: SQLite's column pointers do not survive stepping.
#ifdef DATAFRIEND_PROFILE
    auto start = bench_clock::now();
#endif
    struct row { uint64_t entity; std::string done, title; };
    std::vector<row> rows;
    std::vector<projected> facts;
    if (buffer_facts) facts.reserve(previous.size());
    else rows.reserve(previous.size() / 2);
    select->reset(); select->text(1, std::to_string(group));
    while (select->row()) {
      auto id = uint64_t(sqlite3_column_int64(select->stmt, 0));
      if (buffer_facts) {
        facts.push_back({uint32_t(id), done_id, select->column(1)});
        facts.push_back({uint32_t(id), title_id, select->column(2)});
      } else rows.push_back({id, select->column(1), select->column(2)});
    }
#ifdef DATAFRIEND_PROFILE
    profile.sql_read_us += micros(bench_clock::now() - start);
    profile.rows += buffer_facts ? facts.size() / 2 : rows.size();
    start = bench_clock::now();
#endif
    auto result = materialize(previous, [&](auto&& emit) {
      for (const auto& fact : facts) emit(fact.entity,fact.attribute,fact.value);
      for (const auto& r : rows) {
        emit(uint32_t(r.entity), done_id, r.done); emit(uint32_t(r.entity), title_id, r.title);
      }
    });
#ifdef DATAFRIEND_PROFILE
    profile.view_us += micros(bench_clock::now() - start);
    start = bench_clock::now();
#endif
    std::vector<row>().swap(rows); // Account for row destruction and buffer release.
    std::vector<projected>().swap(facts);
#ifdef DATAFRIEND_PROFILE
    profile.buffer_release_us += micros(bench_clock::now() - start);
#endif
    return result;
  }
  void modify(edit changes) {
#ifdef DATAFRIEND_PROFILE
    profile = {};
#endif
    require(changes.remove.empty() && changes.add.size()==1,"one replacement required");
    const auto& t=changes.add.at(0);
    require(t.entity==1&&t.attribute=="done","unsupported edit");
    begin->reset(); begin->done();
    try {
      update->reset(); update->text(1, t.value);
      update->integer(2, t.entity); update->done();
      require(sqlite3_changes(connection.db) == 1, "expected one updated entity");
#ifdef DATAFRIEND_PROFILE
      const auto query_start = bench_clock::now();
#endif
      for (size_t i = 0; i < roots.size(); ++i) evaluate(i + 1, roots[i]);
#ifdef DATAFRIEND_PROFILE
      profile.query_us = micros(bench_clock::now() - query_start);
#endif
      commit->reset(); commit->done();
    } catch (...) {
      sqlite3_exec(connection.db, "ROLLBACK", nullptr, nullptr, nullptr);
      for(size_t i=0;i<roots.size();++i) evaluate(i+1,roots[i]);
      throw;
    }
  }
  std::vector<sql_result> snapshots() {
    std::vector<sql_result> result;
    for (auto index : handles) result.push_back(roots.at(index));
    return result;
  }
  void verify_storage() {
    statement integrity(connection.db, "PRAGMA integrity_check");
    require(integrity.row() && integrity.column(0) == "ok", "SQLite integrity check failed");
    for (size_t group = 1; group <= roots.size(); ++group) {
      auto fresh = evaluate(group, sql_result{scope});
      require(fresh.size() == roots[group - 1].size() &&
              fresh.fingerprint() == roots[group - 1].fingerprint(), "SQL/view mismatch");
    }
  }
};

static void verify_views(const std::vector<sql_result>& views, size_t entities, size_t extra,
                         const std::string& done) {
  require(views.size() == 10 + extra, "wrong subscription count");
  for (size_t i = 0; i < views.size(); ++i) {
    const size_t group = i < 10 ? 1 : i - 8;
    uint64_t expected_size = 0, expected_hash = 0;
    for (uint64_t id = group; id <= entities; id += 100) {
      for (const auto& [attribute, value] : entity_record{
               {"done", id == 1 ? done : "false"}, {"title", "task"}}) {
        triple t{id, attribute, value}; auto found=views[i].lookup(uint32_t(id),attribute);
        require(found && views[i].decode(*found)==t,"wrong published fact");
        const auto key=pair_key(uint32_t(id),*views[i].scope()->find(attribute));
        expected_hash ^= triple_hash(key,boxed_value(value)); ++expected_size;
      }
    }
    require(views[i].size() == expected_size && views[i].fingerprint() == expected_hash,
            "wrong view size/fingerprint");
    if (i < 10) require(views[0].shares_with(views[i]), "identical handles must share root");
  }
}

struct temp_directory {
  std::string path;
  temp_directory() {
    char pattern[] = "/tmp/datafriend-sqlite-XXXXXX";
    require(mkdtemp(pattern) != nullptr, "mkdtemp failed"); path = pattern;
  }
  ~temp_directory() { std::filesystem::remove_all(path); }
};

int main(int argc, char** argv) try {
  require(argc >= 3 && argc <= 6, "usage: sqlite_comparison datafriend|sqlite_eav|sqlite_tasks EXTRA [ENTITIES=10000] [SAMPLES=1000] [stream|buffered|facts|detailed]");
  const std::string backend = argv[1];
  const std::string mode = argc > 5 ? argv[5] : "stream";
  require(mode == "stream" || mode == "buffered" || (mode == "facts" && backend != "datafriend") ||
          (mode == "detailed" && backend == "datafriend"), "unsupported evaluation mode");
  if (backend == "datafriend") {
#ifdef DATAFRIEND_DETAIL_VIEWS
    require(mode == "detailed", "this diagnostic build requires Datafriend detailed mode");
#elif defined(DATAFRIEND_BUFFER_QUERIES)
    require(mode == "buffered", "this diagnostic build requires Datafriend buffered mode");
#else
    require(mode == "stream", "Datafriend buffered mode requires DATAFRIEND_BUFFER_QUERIES=ON");
#endif
  }
  const size_t extra = std::stoul(argv[2]), entities = argc > 3 ? std::stoul(argv[3]) : 10000;
  const size_t samples = argc > 4 ? std::stoul(argv[4]) : 1000, warmup = 100;
  require(extra <= 98 && entities <= UINT32_MAX && entities >= extra + 1 && samples > 0, "invalid workload dimensions");
  require(backend == "datafriend" || backend == "sqlite_eav" || backend == "sqlite_tasks", "unknown backend");
  temp_directory directory;
  std::unique_ptr<engine> native;
  std::unique_ptr<serial_executor> executor;
  std::unique_ptr<sqlite_store> sql;
  std::vector<uint64_t> native_views;
  if (backend == "datafriend") {
    native = std::make_unique<engine>(directory.path, false);
    auto all = native->subscribe({{},{},true}); edit seed;
    for (uint64_t id = 1; id <= entities; ++id) {
      seed.add.push_back({id,"group",std::to_string(id % 100)});
      seed.add.push_back({id,"done","false"}); seed.add.push_back({id,"title","task"});
    }
    native->modify(all, std::move(seed)); native->unsubscribe(all);
    for (size_t i = 0; i < 10 + extra; ++i) {
      const auto group = std::to_string(i < 10 ? 1 : i - 8);
      native_views.push_back(native->subscribe({{{{0,{}},"group",{-1,group}},
          {{0,{}},"done",{1,{}}},{{0,{}},"title",{2,{}}}},{1,2}}));
    }
  } else {
    executor = std::make_unique<serial_executor>();
    executor->call([&] { sql = std::make_unique<sqlite_store>(directory.path + "/store.sqlite",
                                         backend == "sqlite_eav", entities, extra, mode); });
  }
  auto snapshots = [&] {
    if (sql) return executor->call([&] { return sql->snapshots(); });
    std::vector<sql_result> result;
    for (size_t i=0;i<native_views.size();++i) {
      if(i>0&&i<10) result.push_back(result[0]);
      else result.emplace_back(native->result(native_views[i]));
    }
    return result;
  };
  auto modify = [&](size_t i) {
    edit change{{},{{1,"done",i % 2 ? "false" : "true"}}};
    if (sql) executor->call([&] { sql->modify(std::move(change)); });
    else native->modify(std::move(change));
  };
  // Check both toggle directions before timing. Native verification copies are outside timing.
  {
    auto old = snapshots(); verify_views(old, entities, extra, "false");
    modify(0); auto changed = snapshots(); verify_views(changed, entities, extra, "true");
    if(sql) verify_views(old, entities, extra, "true");
    modify(1); verify_views(snapshots(), entities, extra, "false");
  }
  std::vector<double> times, query_times, sql_times, view_times, release_times, query_other, outside_queries;
#ifdef DATAFRIEND_PROFILE
  const std::pair<const char*, uint64_t edit_profile::*> view_fields[] = {
    {"view_pair_keys", &edit_profile::view_keys_ns}, {"view_seen_set", &edit_profile::view_seen_ns},
    {"view_lookup_compare", &edit_profile::view_lookup_ns}, {"view_actual_writes", &edit_profile::view_write_ns},
    {"view_removal_scan", &edit_profile::view_prune_ns}, {"view_cleanup", &edit_profile::view_cleanup_ns}};
  std::vector<std::vector<double>> view_details(std::size(view_fields));
#endif
  times.reserve(samples); query_times.reserve(samples);
  for (size_t i = 0; i < warmup + samples; ++i) {
    const auto start = bench_clock::now(); modify(i);
    const auto elapsed = micros(bench_clock::now() - start);
#ifdef DATAFRIEND_PROFILE
    sqlite_store::timings profile;
    if (sql) profile = executor->call([&] { return sql->profile; });
    else {
      const auto p = native->last_profile();
      profile = {double(p.queries_ns)/1000, double(p.query_match_ns)/1000,
                 double(p.query_view_ns)/1000, double(p.query_buffer_release_ns)/1000, p.emitted};
      require(p.query_tree_writes == 1, "completion toggle must replace exactly one published fact");
      require(p.previous_entries == p.emitted, "expected unchanged result membership");
      if (mode == "detailed" && i >= warmup)
        for (size_t j=0; j<std::size(view_fields); ++j)
          view_details[j].push_back(double(p.*view_fields[j].second)/1000);
    }
    if (mode != "stream") {
      uint64_t expected_rows = 0;
      for (size_t group = 1; group <= extra + 1; ++group)
        expected_rows += (entities - group) / 100 + 1;
      require(profile.rows == expected_rows * (sql ? 1 : 2), "must consume every expected row/projected fact");
    }
    if (i >= warmup) {
      query_times.push_back(profile.query_us);
      outside_queries.push_back(elapsed - profile.query_us);
      if (mode != "stream") {
        sql_times.push_back(profile.sql_read_us); view_times.push_back(profile.view_us);
        release_times.push_back(profile.buffer_release_us);
        query_other.push_back(profile.query_us - profile.sql_read_us - profile.view_us - profile.buffer_release_us);
      }
    }
#endif
    if (i >= warmup) times.push_back(elapsed);
  }
  verify_views(snapshots(), entities, extra, (warmup + samples - 1) % 2 ? "false" : "true");
  if (sql) executor->call([&] { sql->verify_storage(); });
  else require(native->last_recomputed() == extra + 1, "wrong query evaluation count");
  std::cout << "backend,mode,entities,handles,queries,samples,phase,mean_us,p50_us,p95_us,p99_us,max_us\n";
  auto report = [&](const char* phase, std::vector<double>& values) {
    if (values.empty()) return;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    std::sort(values.begin(), values.end());
    std::cout << backend << ',' << mode << ',' << entities << ',' << 10 + extra << ',' << 1 + extra << ',' << samples
              << ',' << phase << ',' << mean << ',' << values[values.size()/2] << ','
              << values[values.size()*95/100] << ',' << values[values.size()*99/100] << ',' << values.back() << '\n';
  };
  report("call_total", times); report("queries_and_views", query_times);
  report(sql ? (mode == "facts" ? "sql_read_owned_facts" : "sql_read_owned_rows") : "query_match_borrowed_facts", sql_times);
  report("view_maintenance", view_times);
  report("result_buffer_release", release_times); report("query_other", query_other);
  report("outside_queries", outside_queries);
#ifdef DATAFRIEND_PROFILE
  for (size_t j=0; j<std::size(view_fields); ++j) report(view_fields[j].first, view_details[j]);
#endif
  if (sql) executor->call([&] { sql.reset(); });
  std::cerr << "verification=passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "benchmark failed: " << error.what() << '\n'; return 1;
}
