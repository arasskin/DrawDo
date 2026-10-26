#pragma once
namespace index_sync {
inline reconciliation::source source(record_index::header *h) {
  return lb_index_source(h);
}
} // namespace index_sync
