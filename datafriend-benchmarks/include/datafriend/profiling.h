#pragma once
#include <cstdint>
namespace datafriend {
// Only present in explicitly instrumented builds; no timers in production.
struct edit_profile {
  uint64_t owner_ns=0, working_ns=0, reconcile_ns=0, read_ns=0;
  uint64_t stage_ns=0, indexes_ns=0, queries_ns=0, commit_ns=0, publish_ns=0;
  uint64_t routing_old_ns=0, index_update_ns=0, routing_new_ns=0;
  uint64_t routing_lookup_ns=0, routing_maintenance_ns=0;
  // Optional fine-grained clocks; their overhead is measured against coarse-only.
  uint64_t index_lookup_ns=0, index_ave_remove_ns=0, index_aev_remove_ns=0;
  uint64_t index_box_ns=0, index_aev_insert_ns=0, index_ave_insert_ns=0;
  uint64_t query_match_ns=0, query_view_ns=0, query_buffer_release_ns=0;
  uint64_t view_keys_ns=0, view_seen_ns=0, view_lookup_ns=0;
  uint64_t view_write_ns=0, view_prune_ns=0, view_cleanup_ns=0;
  uint64_t candidates=0, emitted=0, previous_entries=0, query_tree_writes=0;
};
}
