#pragma once
#include "reconcile.h"
#include "index.h"

// Adapter for the existing key-ordered index. IDs are record.key; individual
// fingerprints are record.hash, and range XORs come from the cached summaries.
// Keep the tree header and snapshot stable throughout the exchange.
namespace index_sync {
inline record_index::key_range bounds(reconciliation::range part) {
  uint64_t high;
  if (reconciliation::upper_bound(part, &high)) return {part.low, high};
  return {part.low, std::nullopt};
}
inline bool summarize(void *context, reconciliation::range part, reconciliation::summary *out) {
  auto *tree = static_cast<record_index::header *>(context);
  const auto result = record_index::summarize(tree, bounds(part));
  *out = {result.count, result.hash};
  return true;
}
inline bool select(void *context, uint64_t rank, uint64_t *out) {
  const auto item = record_index::select(static_cast<record_index::header *>(context), rank);
  if (!item) return false;
  *out = item->key;
  return true;
}
inline size_t list(void *context, reconciliation::range part, reconciliation::item *out, size_t capacity) {
  auto *tree = static_cast<record_index::header *>(context);
  const auto range = bounds(part);
  const uint64_t first = record_index::summarize(tree, {0, part.low}).count;
  const uint64_t count = record_index::summarize(tree, range).count;
  const size_t length = size_t(std::min(uint64_t(capacity), count));
  for (size_t i = 0; i < length; ++i) {
    const auto item = record_index::select(tree, first + i);
    if (!item) return SIZE_MAX;
    out[i] = {item->key, item->hash};
  }
  return length;
}
inline reconciliation::source source(record_index::header *tree) { return {tree, summarize, select, list}; }
}
