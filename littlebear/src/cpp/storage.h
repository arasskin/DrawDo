#pragma once
namespace storage {
namespace table = hash_table;
namespace tree = record_index;
namespace heap = file_heap;
using store = lb_store;
using request = lb_store_request;
using result = lb_store_result;
using io_operation = lb_store_io_operation;
using namespace_id = lb_store_namespace_id;
using record_key = lb_store_record_key;
struct error {
  static constexpr auto ok = lb_error_ok;
  static constexpr auto invalid_argument = lb_error_invalid_argument;
  static constexpr auto not_open = lb_error_not_open;
  static constexpr auto namespace_missing = lb_error_namespace_missing;
  static constexpr auto record_missing = lb_error_record_missing;
  static constexpr auto busy = lb_error_busy;
  static constexpr auto no_memory = lb_error_no_memory;
  static constexpr auto capacity = lb_error_capacity;
  static constexpr auto buffer_too_small = lb_error_buffer_too_small;
  static constexpr auto io_failure = lb_error_io_failure;
  static constexpr auto more_io = lb_error_more_io;
  static constexpr auto more_work = lb_error_more_work;
  static constexpr auto cancelled = lb_error_cancelled;
  static constexpr auto invalid_state = lb_error_invalid_state;
};
struct phase {
  static constexpr auto idle = lb_phase_idle;
  static constexpr auto ready = lb_phase_ready;
  static constexpr auto submitted = lb_phase_submitted;
  static constexpr auto finished = lb_phase_finished;
};
struct presence {
  static constexpr auto absent = lb_presence_absent;
  static constexpr auto empty = lb_presence_empty;
  static constexpr auto populated = lb_presence_populated;
};
inline result create(store *s, const char *path, uint64_t file_limit, size_t pending_limit = 64,
                     size_t initial_namespaces = table::DEFAULT_INITIAL_CAPACITY,
                     tree::pool::system_allocate_fn index_allocate = nullptr,
                     table::allocate_fn namespace_allocate = table::system_allocate) {
  return lb_store_create(s, path, file_limit, pending_limit, initial_namespaces, index_allocate,
                         namespace_allocate);
}
inline result destroy(store *s) {
  return lb_store_destroy(s);
}
inline bool namespace_busy(store *s, namespace_id ns) {
  return lb_store_namespace_busy(s, ns);
}
inline bool record_busy(store *s, namespace_id ns, record_key key, bool writing) {
  return lb_store_record_busy(s, ns, key, writing);
}
inline lb_store_presence lookup_namespace(store *s, namespace_id ns) {
  return lb_store_lookup_namespace(s, ns);
}
inline result create_namespace(store *s, namespace_id ns) {
  return lb_store_create_namespace(s, ns);
}
inline result erase_record(store *s, namespace_id ns, record_key key) {
  return lb_store_erase_record(s, ns, key);
}
inline result clear_namespace(store *s, namespace_id ns, size_t record_budget = 256) {
  return lb_store_clear_namespace(s, ns, record_budget);
}
inline result remove_namespace(store *s, namespace_id ns, size_t record_budget = 256) {
  return lb_store_remove_namespace(s, ns, record_budget);
}
inline void attach(store *s, request *r) {
  return lb_store_attach(s, r);
}
inline result prepare_read(store *s, request *r, namespace_id ns, record_key key, void *buffer,
                           size_t capacity) {
  return lb_store_prepare_read(s, r, ns, key, buffer, capacity);
}
inline result prepare_write(store *s, request *r, namespace_id ns, record_key key,
                            const void *buffer, size_t length, uint64_t fingerprint) {
  return lb_store_prepare_write(s, r, ns, key, buffer, length, fingerprint);
}
inline result start_io(request *r, io_operation *operation) {
  return lb_store_start_io(r, operation);
}
inline result finish(request *r, result outcome) {
  return lb_store_finish(r, outcome);
}
inline result complete_io(request *r, int completed_bytes) {
  return lb_store_complete_io(r, completed_bytes);
}
inline result cancel(request *r) {
  return lb_store_cancel(r);
}
} // namespace storage
