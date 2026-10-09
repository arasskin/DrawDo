#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// Persistent mirror target over AELMDB: one aggregate-indexed dbi holding the
// full app state as a keyed set. The B+tree itself is the negentropy sync
// structure — ordered by key, hash-aggregated per node.
//
//   key   = [updated_at BE64 | id (32 bytes)]     (40 bytes)
//   value = payload (opaque serialized entity)
//
// The store is deliberately dumb: it resolves nothing. Version resolution
// happens in the in-memory database (the source of truth); the merge-walk
// between memory and this store emits exact put/del keys, including the
// del(old)+put(new) pair for an edited entity. Duplicate ids at different
// timestamps are representable (e.g. after an interrupted mirror) and are
// healed by the next walk; restore paths should let the later timestamp win.
//
// All functions throw std::runtime_error on database failure. Single-threaded
// use assumed until the network mirror introduces its threading story.

namespace store {

inline constexpr size_t ID_SIZE = 32;  // negentropy requires 32-byte ids
using Id = std::array<uint8_t, ID_SIZE>;

struct Item {
    Id id{};
    uint64_t updated_at = 0;  // ordering value (e.g. unix ms); part of the key
    std::string payload;
};

struct Store;       // opaque; defined in store.cpp
struct WriterImpl;  // opaque; txn + dbi of the transaction in flight

// Open (creating if necessary) the store at dir. First-time schema setup
// (aggregate flags + hash offset) is performed idempotently.
Store* open(const std::string& dir, size_t mapsize_bytes = 64ull << 20);
void close(Store* s);

// Mutations run inside one atomic transaction: commits if fn returns
// normally, aborts (and rethrows) if fn throws.
class Writer {
  public:
    void put(uint64_t updated_at, const Id& id, std::string_view payload);
    void del(uint64_t updated_at, const Id& id);  // no-op if absent (idempotent)
  private:
    friend void write(Store*, const std::function<void(Writer&)>&);
    explicit Writer(WriterImpl* impl) : impl_(impl) {}
    WriterImpl* impl_;
};
void write(Store* s, const std::function<void(Writer&)>& fn);

// Iterate every item in (updated_at, id) key order — the same order the
// memory-side merge-walk uses.
void load_all(Store* s, const std::function<void(const Item&)>& cb);

// Point read of one exact (updated_at, id) key. Returns false if absent.
bool get(Store* s, uint64_t updated_at, const Id& id, std::string* payload_out);

uint64_t item_count(Store* s);

}  // namespace store
