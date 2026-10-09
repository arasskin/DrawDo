// Benchmark-only types for the UNCHANGED Negentropy BTreeCore algorithms.
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace negentropy_xor {
using err = std::runtime_error;
struct Item {
  uint64_t timestamp = 0, value = 0, hash = 0;
  bool operator==(const Item& b) const { return timestamp == b.timestamp; }
  bool operator<(const Item& b) const { return timestamp < b.timestamp; }
  bool operator<=(const Item& b) const { return timestamp <= b.timestamp; }
  Item() = default;
  Item(uint64_t key, std::string_view bytes) : timestamp(key) {
    if (bytes.size() != 16) throw err("expected value/hash");
    std::memcpy(&value, bytes.data(), 8); std::memcpy(&hash, bytes.data()+8, 8);
  }
};
struct Bound { Item item; explicit Bound(uint64_t key) { item.timestamp = key; } };
struct Fingerprint { uint64_t hash = 0; };
struct Accumulator {
  uint64_t hash = 0;
  void setToZero() { hash = 0; }
  void add(const Item& i) { hash ^= i.hash; }
  void add(const Accumulator& a) { hash ^= a.hash; }
  void sub(const Item& i) { hash ^= i.hash; }
  void sub(const Accumulator& a) { hash ^= a.hash; }
  void negate() {} // XOR is its own inverse
  Fingerprint getFingerprint(uint64_t) { return {hash}; }
};
struct StorageBase {
  virtual uint64_t size() = 0;
  virtual const Item& getItem(size_t) = 0;
  virtual void iterate(size_t, size_t, std::function<bool(const Item&, size_t)>) = 0;
  virtual size_t findLowerBound(size_t, size_t, const Bound&) = 0;
  virtual Fingerprint fingerprint(size_t, size_t) = 0;
};
} // namespace negentropy_xor
