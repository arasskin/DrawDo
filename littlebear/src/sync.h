#ifndef LB_SYNC_H
#define LB_SYNC_H
#include "storage.h"
#include "index_sync.h"
#include "exchange.h"
/* One loop owns storage and metadata. Every resumed operation re-looks-up the
 * namespace because another client may have grown the dense table. */
typedef struct lb_service {
  lb_store cache;
  uint64_t epoch, generation;
  size_t delta_bytes;
} lb_service;
typedef struct lb_change {
  uint64_t id;
  lb_bytes value;
  bool present;
} lb_change;
typedef enum { lb_task_new, lb_task_read, lb_task_write, lb_task_done } lb_task_phase;
typedef struct lb_task {
  lb_service *service;
  lb_namespace ns;
  unsigned op;
  bool writable, locked, absent, has_expected;
  uint64_t epoch, version, expected, before;
  lb_buffer payload, reply, records, read_buffer;
  lb_bytes records_payload;
  lb_change *changes;
  size_t count, position;
  lb_store_request io;
  lb_task_phase phase;
} lb_task;
static inline lb_namespace lb_name(const uint8_t name[16]) {
  lb_namespace ns;
  memcpy(&ns, name, 16);
  return ns;
}
static inline lb_namespace_entry *lb_service_entry(lb_service *s, lb_namespace ns) {
  return lb_directory_entry(&s->cache.namespaces, ns);
}
static inline uint64_t lb_service_revision(lb_service *s, lb_namespace ns) {
  lb_namespace_entry *e = lb_service_entry(s, ns);
  return e ? e->revision : 0;
}
static inline bool lb_service_writing(lb_service *s, lb_namespace ns) {
  lb_namespace_entry *e = lb_service_entry(s, ns);
  return e && e->writing;
}
static inline uint64_t lb_service_fingerprint(lb_service *s, lb_namespace ns) {
  lb_namespace_entry *e = lb_service_entry(s, ns);
  return e ? ct_node_hash(e->value.root) : 0;
}
static inline lb_store_result lb_service_create(lb_service *s, const char *path, uint64_t epoch,
                                                uint64_t capacity) {
  lb_store_result rc = lb_store_create(&s->cache, path, capacity, 64, 0, NULL, ct_system_allocate);
  if (rc.code == lb_error_ok)
    s->epoch = epoch;
  return rc;
}
static inline void lb_service_destroy(lb_service *s) {
  lb_store_result rc = lb_store_destroy(&s->cache);
  assert(rc.code == lb_error_ok);
  (void)rc;
}
static inline void lb_reply_header(lb_buffer *out, uint8_t status, uint64_t epoch,
                                   uint64_t revision) {
  lb_buffer_clear(out);
  lb_u8(out, status);
  lb_u64(out, epoch);
  lb_u64(out, revision);
}
static inline bool lb_opening(lb_service *s, lb_namespace ns, uint8_t status, lb_buffer *out) {
  lb_namespace_entry *e = lb_service_entry(s, ns);
  bool busy = e && e->writing;
  lb_reply_header(out, busy ? 1 : e ? status : 3, s->epoch, e ? e->revision : 0);
  if (e && !busy && !lb_exchange_begin(lb_index_source(&e->value), out))
    return false;
  return !out->failed;
}
static inline void lb_forget_delta(lb_service *s, lb_namespace ns) {
  lb_namespace_entry *e = lb_service_entry(s, ns);
  if (!e)
    return;
  s->delta_bytes -= e->delta.size;
  lb_buffer_free(&e->delta);
}
static inline void lb_remember_delta(lb_service *s, lb_namespace ns, uint64_t before,
                                     uint64_t after, lb_bytes records) {
  lb_namespace_entry *e;
  lb_buffer out = CT_ZERO;
  size_t i;
  lb_forget_delta(s, ns);
  if (records.size > 256 * 1024 + 65536)
    return;
  lb_u64(&out, lb_service_fingerprint(s, ns));
  lb_append(&out, records.data, records.size);
  if (out.failed) {
    lb_buffer_free(&out);
    return;
  }
  if (s->delta_bytes + out.size > 8 * 1024 * 1024) {
    for (i = 0; i < s->cache.namespaces.count; ++i)
      lb_buffer_free(&s->cache.namespaces.entries[i].delta);
    s->delta_bytes = 0;
  }
  e = lb_service_entry(s, ns);
  if (!e) {
    lb_buffer_free(&out);
    return;
  }
  e->delta = out;
  e->delta_before = before;
  e->delta_after = after;
  s->delta_bytes += out.size;
}
static inline void lb_task_unlock(lb_task *t) {
  if (t->locked) {
    lb_namespace_entry *e = lb_service_entry(t->service, t->ns);
    assert(e);
    e->writing = false;
    t->locked = false;
    ++t->service->generation;
  }
}
static inline void lb_task_dispose(lb_task *t) {
  if (t->io.owner) {
    if (t->io.state == lb_phase_submitted)
      abort();
    lb_store_cancel(&t->io);
  }
  lb_task_unlock(t);
  lb_buffer_free(&t->payload);
  lb_buffer_free(&t->reply);
  lb_buffer_free(&t->records);
  lb_buffer_free(&t->read_buffer);
  free(t->changes);
  memset(t, 0, sizeof(*t));
}
static inline void lb_task_reject(lb_task *t) {
  if (t->io.owner) {
    if (t->io.state == lb_phase_submitted)
      abort();
    lb_store_cancel(&t->io);
  }
  lb_reply_header(&t->reply, 2, 0, 0);
  lb_blob(&t->reply, lb_bytes_make("request rejected", 16));
  t->phase = lb_task_done;
  lb_task_unlock(t);
}
static inline bool lb_task_init(lb_task *t, lb_service *s, unsigned op, lb_namespace ns,
                                bool writable, uint64_t epoch, uint64_t revision,
                                lb_bytes payload) {
  memset(t, 0, sizeof(*t));
  t->service = s;
  t->op = op;
  t->ns = ns;
  t->writable = writable;
  t->epoch = epoch;
  t->version = revision;
  if (!lb_append(&t->payload, payload.data, payload.size)) {
    lb_task_reject(t);
    return false;
  }
  return true;
}
static inline void lb_task_ready(lb_task *t, lb_buffer *reply) {
  memset(t, 0, sizeof(*t));
  t->reply = *reply;
  memset(reply, 0, sizeof(*reply));
  t->phase = lb_task_done;
}
static inline void lb_task_restart(lb_task *t) {
  if (!lb_opening(t->service, t->ns, 1, &t->reply))
    lb_task_reject(t);
  else {
    t->phase = lb_task_done;
    lb_task_unlock(t);
  }
}
static inline int lb_change_order(const void *a, const void *b) {
  uint64_t x = ((const lb_change *)a)->id, y = ((const lb_change *)b)->id;
  return (x > y) - (x < y);
}
static inline bool lb_task_parse_changes(lb_task *t, bool writing) {
  lb_reader r = {lb_buffer_bytes(&t->payload), false};
  uint32_t count;
  size_t i;
  if (t->op == 6) {
    t->has_expected = true;
    t->expected = lb_read_u64(&r);
  }
  t->records_payload = r.data;
  count = lb_read_u32(&r);
  if (r.failed || count > LB_MAX_RECORDS || count > r.data.size / (writing ? 9 : 8))
    return false;
  if (count) {
    t->changes = (lb_change *)calloc(count, sizeof(*t->changes));
    if (!t->changes)
      return false;
  }
  t->count = count;
  for (i = 0; i < count; ++i) {
    lb_change *c = &t->changes[i];
    c->id = lb_read_u64(&r);
    if (writing) {
      uint8_t present = lb_read_u8(&r);
      if (present > 1)
        return false;
      c->present = present != 0;
      if (c->present) {
        uint8_t key[8];
        unsigned j;
        XXH64_state_t hash;
        c->value = lb_read_blob(&r);
        if (c->value.size < 8 || c->value.size > 65535)
          return false;
        for (j = 0; j < 8; ++j)
          key[j] = (uint8_t)(c->id >> (56 - 8 * j));
        XXH64_reset(&hash, 0);
        XXH64_update(&hash, key, 8);
        XXH64_update(&hash, c->value.data + 8, c->value.size - 8);
        if (lb_integer(c->value) != XXH64_digest(&hash))
          return false;
      }
    }
  }
  if (!lb_reader_end(&r))
    return false;
  if (count > 1)
    qsort(t->changes, count, sizeof(*t->changes), lb_change_order);
  for (i = 1; i < count; ++i)
    if (t->changes[i - 1].id == t->changes[i].id)
      return false;
  return true;
}
/* Returns after at most one prepared file operation. The task and its buffers
 * remain stable until that operation's CQE, including when the peer disconnects. */
static inline void lb_task_step(lb_task *t) {
  lb_service *s = t->service;
  lb_namespace_entry *entry;
  lb_store_result rc;
  bool writing = t->op == 3 || t->op == 6;
  if (t->phase == lb_task_done)
    return;
  if (t->phase == lb_task_new) {
    uint64_t version = lb_service_revision(s, t->ns);
    t->absent = lb_store_lookup_namespace(&s->cache, t->ns) == lb_presence_absent;
    if (writing && !t->writable) {
      lb_task_reject(t);
      return;
    }
    if (lb_service_writing(s, t->ns)) {
      lb_reply_header(&t->reply, 1, s->epoch, version);
      t->phase = lb_task_done;
      return;
    }
    if (t->op == 0) {
      if (t->payload.size != 1 || t->payload.data[0] > 1) {
        lb_task_reject(t);
        return;
      }
      lb_reply_header(&t->reply, t->absent ? 3 : 0, s->epoch, version);
      t->phase = lb_task_done;
      return;
    }
    if (t->epoch != s->epoch || (t->op != 6 && t->version != version) ||
        (t->absent && t->op == 2)) {
      lb_task_restart(t);
      return;
    }
    t->version = version;
    if (t->op == 1) {
      lb_index empty = CT_ZERO;
      lb_index *tree = lb_directory_get(&s->cache.namespaces, t->ns);
      lb_reply_header(&t->reply, 0, s->epoch, version);
      if (!lb_exchange_receive(lb_index_source(tree ? tree : &empty), lb_buffer_bytes(&t->payload),
                               &t->records) ||
          !lb_append(&t->reply, t->records.data, t->records.size))
        lb_task_reject(t);
      else
        t->phase = lb_task_done;
      return;
    }
    if (t->op != 2 && !writing) {
      lb_task_reject(t);
      return;
    }
    if (!lb_task_parse_changes(t, writing)) {
      lb_task_reject(t);
      return;
    }
    if (writing) {
      t->before = version;
      lb_forget_delta(s, t->ns);
      if (t->absent) {
        rc = lb_store_create_namespace(&s->cache, t->ns);
        if (rc.code != lb_error_ok) {
          lb_task_reject(t);
          return;
        }
      }
      entry = lb_service_entry(s, t->ns);
      entry->writing = true;
      t->locked = true;
      if (t->absent || t->count)
        entry->revision = ++t->version;
      t->phase = lb_task_write;
    } else {
      lb_u32(&t->records, (uint32_t)t->count);
      t->phase = lb_task_read;
    }
  } else if (t->io.state == lb_phase_finished) {
    if (t->io.outcome.code != lb_error_ok) {
      lb_task_reject(t);
      return;
    }
    if (t->phase == lb_task_read) {
      if (lb_service_revision(s, t->ns) != t->version || lb_service_writing(s, t->ns)) {
        lb_task_restart(t);
        return;
      }
      lb_u64(&t->records, t->changes[t->position].id);
      lb_blob(&t->records, lb_bytes_make(t->read_buffer.data, t->io.transferred));
      if (t->records.failed || t->records.size > LB_MAX_FRAME - 32) {
        lb_task_reject(t);
        return;
      }
    }
    ++t->position;
    t->io.state = lb_phase_idle;
  } else if (t->io.owner)
    return;
  while (t->position < t->count) {
    lb_change *c = &t->changes[t->position];
    if (t->phase == lb_task_read) {
      if (lb_service_revision(s, t->ns) != t->version || lb_service_writing(s, t->ns)) {
        lb_task_restart(t);
        return;
      }
      if (!lb_buffer_reserve(&t->read_buffer, 65535)) {
        lb_task_reject(t);
        return;
      }
      rc = lb_store_prepare_read(&s->cache, &t->io, t->ns, c->id, t->read_buffer.data, 65535);
    } else if (c->present)
      rc = lb_store_prepare_write(&s->cache, &t->io, t->ns, c->id, c->value.data, c->value.size,
                                  lb_integer(c->value));
    else {
      rc = lb_store_erase_record(&s->cache, t->ns, c->id);
      if (rc.code == lb_error_record_missing)
        rc.code = lb_error_ok;
    }
    if (rc.code == lb_error_busy) {
      lb_task_restart(t);
      return;
    }
    if (rc.code != lb_error_ok) {
      lb_task_reject(t);
      return;
    }
    if (t->io.owner)
      return;
    ++t->position;
  }
  if (writing) {
    if (t->has_expected && lb_service_fingerprint(s, t->ns) != t->expected) {
      lb_reply_header(&t->reply, 1, s->epoch, t->version);
      entry = lb_service_entry(s, t->ns);
      if (!lb_exchange_begin(lb_index_source(&entry->value), &t->reply))
        lb_task_reject(t);
      else {
        t->phase = lb_task_done;
        lb_task_unlock(t);
      }
      return;
    }
    lb_remember_delta(s, t->ns, t->before, t->version, t->records_payload);
  }
  lb_reply_header(&t->reply, 0, s->epoch, t->version);
  if (!lb_append(&t->reply, t->records.data, t->records.size))
    lb_task_reject(t);
  else {
    t->phase = lb_task_done;
    lb_task_unlock(t);
  }
}
static inline lb_store_result lb_perform(lb_store_request *request) {
  for (;;) {
    lb_store_io_operation op = CT_ZERO;
    lb_store_result rc = lb_store_start_io(request, &op);
    ssize_t n;
    if (rc.code != lb_error_ok)
      return rc;
    n = op.writing ? pwrite(op.fd, op.buffer, op.length, (off_t)op.offset)
                   : pread(op.fd, op.buffer, op.length, (off_t)op.offset);
    rc = lb_store_complete_io(request, n < 0 ? -errno : (int)n);
    if (rc.code != lb_error_more_io)
      return rc;
  }
}
static inline bool lb_handle(lb_service *s, unsigned op, lb_namespace ns, bool writable,
                             uint64_t epoch, uint64_t revision, lb_bytes payload,
                             lb_buffer *reply) {
  lb_task t;
  lb_task_init(&t, s, op, ns, writable, epoch, revision, payload);
  while (t.phase != lb_task_done) {
    lb_task_step(&t);
    if (t.io.owner)
      lb_perform(&t.io);
  }
  *reply = t.reply;
  memset(&t.reply, 0, sizeof(t.reply));
  lb_task_dispose(&t);
  return !reply->failed;
}
#ifdef __cplusplus
#include "cpp/sync.h"
#endif
#endif
