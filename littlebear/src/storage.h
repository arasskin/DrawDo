#pragma once
#include "main_data_containers/hash_table.h"
#include "main_data_containers/endless_virtual_memory_field.h"
#include "size_class_file_heap.h"
#include <cerrno>
#include <sys/file.h>

// Single event-loop owner. The store, active requests, and their buffers must
// remain at stable addresses until completion. No per-record I/O bookkeeping.
namespace storage {
namespace table = hash_table;
namespace tree = augmented_tree;
namespace heap = size_class_file_heap;
namespace arena = geometric_rank_partioned_virtual_slab_arena;
using namespace_id = table::key;
using record_key = tree::key;

enum class error {
  ok, invalid_argument, not_open, namespace_missing, record_missing,
  busy, no_memory, capacity, buffer_too_small, io_failure, more_io,
  more_work, cancelled, invalid_state
};
struct result { error code = error::ok; int system_error = 0; };
enum class presence { absent, empty, populated };
enum class phase { idle, ready, submitted, finished };
struct request;
struct store {
  heap::file_and_allocator values{.file_descriptor = -1, .file_allocator = {}, .has_error = true};
  arena::allocator nodes{};
  uint64_t *hashes = nullptr;
  table::two_way namespaces{};
  request *pending = nullptr;
  size_t pending_count = 0;
  size_t pending_limit = 64;
  bool opened = false;
};
struct request {
  store *owner = nullptr;
  request *next = nullptr;
  namespace_id ns{};
  record_key key = 0;
  tree::value_pointer value = 0;
  tree::value_pointer previous = 0;
  uint64_t fingerprint = 0;
  void *buffer = nullptr;
  size_t transferred = 0;
  int rollback_ticket = -1;
  int retirement_ticket = -1;
  bool writing = false;
  phase state = phase::idle;
  result outcome{};
};
struct io_operation {
  int fd;
  uint64_t offset;
  void *buffer;
  uint32_t length;
  bool writing;
};

// Also used to unwind partial initialization. There must be no outstanding I/O.
inline void release_resources(store *s) {
  table::destroy(&s->namespaces);
  endless_virtual_memory_field::destroy(s->hashes);
  arena::destroy(&s->nodes);
  heap::destroy(&s->values);
  *s = {};
}

// Dedicated disposable cache file, locked before truncation. Call on a fresh or
// destroyed store; it must not subsequently move, because tree headers borrow it.
inline result create(store *s, const char *path, uint64_t file_limit,
                     size_t pending_limit = 64, size_t initial_buckets = table::DEFAULT_BUCKET_COUNT,
                     size_t max_buckets = table::DEFAULT_MAX_BUCKET_COUNT) {
  if (s->opened) return {error::busy};
  if (path == nullptr || file_limit == 0 || file_limit > six_byte_mask + 1 || pending_limit == 0 ||
      !table::valid_bucket_count(initial_buckets) || !table::valid_bucket_count(max_buckets) ||
      initial_buckets > max_buckets) return {error::invalid_argument};
  s->values = heap::create(path, file_limit);
  if (s->values.has_error) return {error::io_failure, errno};
  if (flock(s->values.file_descriptor, LOCK_EX | LOCK_NB) != 0) {
    const int saved = errno;
    release_resources(s);
    return {error::io_failure, saved};
  }
  auto nodes = arena::create();
  s->nodes = nodes.a;
  if (!nodes.has_error) s->hashes = endless_virtual_memory_field::create();
  if (s->hashes != nullptr) s->namespaces = table::create(initial_buckets, max_buckets);
  if (nodes.has_error || s->hashes == nullptr || table::bad(&s->namespaces)) {
    release_resources(s);
    return {error::no_memory};
  }
  if (ftruncate(s->values.file_descriptor, 0) != 0) {
    const int saved = errno;
    release_resources(s);
    return {error::io_failure, saved};
  }
  s->pending_limit = pending_limit;
  s->opened = true;
  return {};
}

inline result destroy(store *s) {
  if (s->pending_count != 0) return {error::busy};
  release_resources(s);
  return {};
}

inline bool namespace_busy(store *s, namespace_id ns) {
  for (request *r = s->pending; r != nullptr; r = r->next)
    if (table::operator==(r->ns, ns)) return true;
  return false;
}

// Different records can issue I/O concurrently, including within one namespace.
// Multiple reads can share a record; mutation waits for all of its reads.
inline bool record_busy(store *s, namespace_id ns, record_key key, bool writing) {
  for (request *r = s->pending; r != nullptr; r = r->next)
    if (table::operator==(r->ns, ns) && r->key == key && (writing || r->writing)) return true;
  return false;
}

inline presence lookup_namespace(store *s, namespace_id ns) {
  auto *index = table::get(&s->namespaces, ns);
  return index == nullptr ? presence::absent : tree::is_empty(index) ? presence::empty : presence::populated;
}

inline result create_namespace(store *s, namespace_id ns) {
  if (!s->opened) return {error::not_open};
  if (table::contains(&s->namespaces, ns)) return {};
  return table::unsafe_insert(&s->namespaces, ns, tree::create(&s->nodes, s->hashes))
    ? result{} : result{error::capacity};
}

inline result erase_record(store *s, namespace_id ns, record_key key) {
  if (!s->opened) return {error::not_open};
  auto *index = table::get(&s->namespaces, ns);
  if (index == nullptr) return {error::namespace_missing};
  if (record_busy(s, ns, key, true)) return {error::busy};
  const uint32_t leaf = tree::find(index, key);
  if (leaf == tree::INVALID_NODE_INDEX) return {error::record_missing};
  const auto value = tree::node_get_data(index, leaf)[tree::node_item::v].value;
  const int ticket = heap::prepare_release(&s->values.file_allocator);
  if (ticket == -1) return {error::no_memory};
  const uint64_t fingerprint = s->hashes[leaf];
  tree::unsafe_dissociate(index, key, fingerprint);
  heap::release_prepared(&s->values.file_allocator, tree::vpointer_get_offset(value),
                         tree::vpointer_get_size(value), ticket);
  return {};
}

// Bounded work per call. more_work means some records remain; an allocation
// failure can also leave a partially cleared namespace. Both are safe to retry.
inline result clear_namespace(store *s, namespace_id ns, size_t record_budget = 256) {
  if (!s->opened) return {error::not_open};
  if (record_budget == 0) return {error::invalid_argument};
  auto *index = table::get(&s->namespaces, ns);
  if (index == nullptr) return {error::namespace_missing};
  if (namespace_busy(s, ns)) return {error::busy};
  while (!tree::is_empty(index) && record_budget-- != 0) {
    uint32_t leaf = index->root;
    while (tree::node_rank(leaf) != -1)
      leaf = tree::node_get_data(index, leaf)[tree::node_item::children].left_index;
    const auto key = tree::node_get_data(index, leaf)[tree::node_item::k].value;
    const result erased = erase_record(s, ns, key);
    if (erased.code != error::ok) return erased;
  }
  return tree::is_empty(index) ? result{} : result{error::more_work};
}

inline result remove_namespace(store *s, namespace_id ns, size_t record_budget = 256) {
  result cleared = clear_namespace(s, ns, record_budget);
  if (cleared.code == error::ok) table::dissociate(&s->namespaces, ns);
  return cleared;
}

inline void attach(store *s, request *r) {
  r->owner = s;
  r->next = s->pending;
  s->pending = r;
  ++s->pending_count;
  r->state = phase::ready;
}

inline result prepare_read(store *s, request *r, namespace_id ns, record_key key,
                           void *buffer, size_t capacity) {
  if (!s->opened) return {error::not_open};
  if (r->owner != nullptr) return {error::busy};
  auto *index = table::get(&s->namespaces, ns);
  if (index == nullptr) return {error::namespace_missing};
  if (record_busy(s, ns, key, false) || s->pending_count >= s->pending_limit) return {error::busy};
  const auto value = tree::get(index, key);
  if (value == tree::NULLVALUEPOINTER) return {error::record_missing};
  if (capacity < tree::vpointer_get_size(value)) return {error::buffer_too_small};
  if (buffer == nullptr) return {error::invalid_argument};
  *r = {};
  r->ns = ns; r->key = key; r->value = value; r->buffer = buffer;
  attach(s, r);
  return {};
}

// The fingerprint is the current internal tree summary, supplied by the caller;
// it is not a negentropy record ID. Buffer contents must stay unchanged until done.
inline result prepare_write(store *s, request *r, namespace_id ns, record_key key,
                            const void *buffer, size_t length, uint64_t fingerprint) {
  if (!s->opened) return {error::not_open};
  if (r->owner != nullptr) return {error::busy};
  if (buffer == nullptr || length == 0 || length > heap::MAX_VALUE_SIZE) return {error::invalid_argument};
  auto *index = table::get(&s->namespaces, ns);
  if (index == nullptr) return {error::namespace_missing};
  if (record_busy(s, ns, key, true) || s->pending_count >= s->pending_limit) return {error::busy};
  const auto previous = tree::get(index, key);
  auto *allocator = &s->values.file_allocator;
  const int rollback = heap::prepare_release(allocator);
  if (rollback == -1) return {error::no_memory};
  const int retirement = previous == 0 ? -1 : heap::prepare_release(allocator);
  if (previous != 0 && retirement == -1) {
    heap::cancel_release(allocator, rollback);
    return {error::no_memory};
  }
  auto allocation = heap::reserve(allocator, length);
  if (allocation.has_error) {
    heap::cancel_release(allocator, rollback);
    heap::cancel_release(allocator, retirement);
    return {error::capacity};
  }
  *r = {};
  r->ns = ns; r->key = key; r->previous = previous;
  r->value = tree::vpointer_create(allocation.offset, uint16_t(length));
  r->fingerprint = fingerprint; r->buffer = const_cast<void *>(buffer);
  r->rollback_ticket = rollback; r->retirement_ticket = retirement; r->writing = true;
  attach(s, r);
  return {};
}

// Obtain one operation and mark it submitted. The driver must eventually report
// its completion exactly once, even after disconnect or kernel cancellation.
inline result start_io(request *r, io_operation *operation) {
  if (r->owner == nullptr || r->state != phase::ready) return {error::invalid_state};
  *operation = {.fd = r->owner->values.file_descriptor,
                .offset = tree::vpointer_get_offset(r->value) + r->transferred,
                .buffer = static_cast<uint8_t *>(r->buffer) + r->transferred,
                .length = uint32_t(tree::vpointer_get_size(r->value) - r->transferred),
                .writing = r->writing};
  r->state = phase::submitted;
  return {};
}

// Internal cleanup: only after the submitted operation has completed, or while
// cancelling a ready request that the kernel cannot access.
inline result finish(request *r, result outcome) {
  store *s = r->owner;
  auto *allocator = &s->values.file_allocator;
  if (r->writing) {
    if (outcome.code == error::ok) {
      heap::cancel_release(allocator, r->rollback_ticket);
      if (r->previous != 0)
        heap::release_prepared(allocator, tree::vpointer_get_offset(r->previous),
                               tree::vpointer_get_size(r->previous), r->retirement_ticket);
    } else {
      heap::release_prepared(allocator, tree::vpointer_get_offset(r->value),
                             tree::vpointer_get_size(r->value), r->rollback_ticket);
      heap::cancel_release(allocator, r->retirement_ticket);
    }
  }
  request **link = &s->pending;
  while (*link != r) link = &(*link)->next;
  *link = r->next;
  --s->pending_count;
  r->owner = nullptr; r->next = nullptr;
  r->rollback_ticket = -1; r->retirement_ticket = -1;
  r->state = phase::finished; r->outcome = outcome;
  return outcome;
}

inline result complete_io(request *r, int completed_bytes) {
  if (r->owner == nullptr || r->state != phase::submitted) return {error::invalid_state};
  if (completed_bytes == -EINTR || completed_bytes == -EAGAIN) {
    r->state = phase::ready;
    return {error::more_io};
  }
  if (completed_bytes < 0)
    return finish(r, {completed_bytes == -ECANCELED ? error::cancelled : error::io_failure,
                      completed_bytes == INT_MIN ? EIO : -completed_bytes});
  const size_t remaining = tree::vpointer_get_size(r->value) - r->transferred;
  if (completed_bytes == 0 || size_t(completed_bytes) > remaining)
    return finish(r, {error::io_failure, EIO});
  r->transferred += completed_bytes;
  if (size_t(completed_bytes) < remaining) {
    r->state = phase::ready;
    return {error::more_io};
  }
  if (r->writing) {
    // Re-look up: unrelated namespaces may have grown or repacked the table.
    auto *index = table::get(&r->owner->namespaces, r->ns);
    if (index == nullptr) return finish(r, {error::invalid_state});
    if (r->previous != 0) {
      if (!tree::replace(index, r->key, r->value, r->fingerprint))
        return finish(r, {error::invalid_state});
    } else if (tree::unsafe_associate(index, r->key, r->value, r->fingerprint) != tree::NO_ERRORS)
      return finish(r, {error::no_memory});
  }
  return finish(r, {});
}

// A submitted buffer cannot be released yet. Cancel it through the I/O driver
// and deliver its original completion before destroying the request or buffer.
inline result cancel(request *r) {
  if (r->owner == nullptr) return {error::invalid_state};
  if (r->state == phase::submitted) return {error::busy};
  return finish(r, {error::cancelled});
}
}
