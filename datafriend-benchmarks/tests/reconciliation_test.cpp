#include "../benchmarks/reconcile.h"
#include <iostream>
#include <map>
#include <random>

using namespace experiment;
template<class Server> void scenario(uint64_t seed, bool empty_client, bool empty_server) {
  record_index client;
  Server server;
  std::map<uint64_t, record> cm, sm;
  std::mt19937_64 rng(seed);
  const size_t count = 1800;
  for (size_t i = 0; i < count; ++i) {
    const auto k = i == 0 ? UINT64_MAX : (i == 1 ? 0 : permute(i));
    auto e = make_record(k, rng());
    if (!empty_client && rng()%8) { client.set(e); cm[k] = e; }
    if (!empty_server && rng()%8) {
      if (rng()%10 == 0) e = make_record(k, rng());
      server.set(e); sm[k] = e;
    }
  }
  std::set<uint64_t> expected;
  for (auto [k, v] : cm) if (!sm.contains(k) || sm[k] != v) expected.insert(k);
  for (auto [k, v] : sm) if (!cm.contains(k) || cm[k] != v) expected.insert(k);
  auto result = reconciliation::run(client, server);
  require(result.differences == expected, "missed or spurious difference");
  // Client is authoritative. Apply only discovered differences; no tombstones.
  for (const auto k : result.differences) {
    if (auto e = client.get(k)) server.set(*e); else server.erase(k);
  }
  require(client.list({}, SIZE_MAX) == server.list({}, SIZE_MAX), "failed authoritative convergence");
  require(reconciliation::run(client, server).differences.empty(), "not idempotent");
}
struct scan_binary : binary_index { scan_binary() : binary_index(true) {} };
int main() {
  try {
    for (uint64_t seed : {1, 7, 999}) {
      for (auto empty : {std::pair{false,false}, {true,false}, {false,true}, {true,true}}) {
        scenario<record_index>(seed, empty.first, empty.second);
        scenario<binary_index>(seed, empty.first, empty.second);
        scenario<scan_binary>(seed, empty.first, empty.second);
      }
    }
    // Structural equality of keys alone must not hide content swaps.
    record_index a, b;
    a.set(make_record(1, 10)); a.set(make_record(2, 20));
    b.set(make_record(1, 20)); b.set(make_record(2, 10));
    require(reconciliation::run(a,b).differences == std::set<uint64_t>{1,2}, "value swaps missed");
    const auto encoded = reconciliation::encode({{reconciliation::kind::summary, {}, 17, {}}})[0];
    for (size_t size = 0; size < encoded.size(); ++size) {
      bool rejected = false;
      try { (void)reconciliation::decode(std::span(encoded.data(), size)); }
      catch (const std::runtime_error&) { rejected = true; }
      require(rejected, "accepted truncated message");
    }
    std::cout << "PASS: encoded reconciliation, three split policies, updates/deletes/additions, empty peers, max key, convergence\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
