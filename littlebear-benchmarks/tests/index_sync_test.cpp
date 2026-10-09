#include "check.h"
#include "index_sync.h"
#include <set>
#include <vector>

int main() {
  namespace r = reconciliation;
  record_index::pool nodes;
  auto a = record_index::create(&nodes), b = record_index::create(&nodes);
  std::set<uint64_t> expected_have, expected_need, got_have, got_need;
  for (uint64_t i = 1; i < 4000; ++i) {
    const uint64_t id = i, hash = record_index::key_hash(i);
    const bool changed = i >= 1500 && i < 1600;
    if (i < 3000) CHECK(record_index::unsafe_associate(&a, id, 1, hash) == record_index::NO_ERRORS);
    if (i >= 1000) CHECK(record_index::unsafe_associate(&b, id, 1, changed ? record_index::key_hash(i + 4000) : hash) == record_index::NO_ERRORS);
    if (i < 1000 || changed) expected_have.insert(id);
    if (i >= 3000 || changed) expected_need.insert(id);
  }
  using packet = std::vector<uint8_t>;
  r::workspace work{};
  uint8_t memory[4096];
  r::output out{memory, sizeof(memory)};
  CHECK(r::begin(index_sync::source(&a), &work, &out) == r::status::complete);
  std::vector<packet> packets{{memory, memory + out.size}};
  bool to_b = true;
  size_t turns = 0;
  while (!packets.empty()) {
    CHECK(++turns < 100);
    std::vector<packet> next;
    for (auto &input : packets) {
      r::cursor cursor{};
      uint64_t have[32], need[32];
      r::differences differences{have, 32, 0, need, 32, 0};
      for (;;) {
        const auto code = r::receive(index_sync::source(to_b ? &b : &a), to_b ? r::role::responder : r::role::initiator,
          {input.data(), input.size()}, &cursor, &work, &out, &differences, 3);
        CHECK(code == r::status::complete || code == r::status::more || code == r::status::output_full || code == r::status::differences_full);
        if (!to_b) {
          got_have.insert(have, have + differences.have_count);
          got_need.insert(need, need + differences.need_count);
        }
        if (out.size > sizeof(r::MAGIC)) next.emplace_back(memory, memory + out.size);
        if (code == r::status::complete) break;
      }
    }
    packets = std::move(next); to_b = !to_b;
  }
  CHECK(got_have == expected_have && got_need == expected_need);
  record_index::validate(&a); record_index::validate(&b);
  record_index::destroy(&a); record_index::destroy(&b);
  std::puts("PASS: key-ordered wide-tree reconciliation with independent hashes and 100 same-key content edits");
}
