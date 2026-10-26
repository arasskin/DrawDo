#include "document.h"
#include <stdio.h>
#ifdef CRDT_DEMO_TIMING
#include "text_buffers/trace.h"
#endif
static bool fail(document *d, const char *message) {
  snprintf(d->error, sizeof(d->error), "%s", message);
  return false;
}
static bool publish(document *d) {
  if (!sequence_state(&d->sequence, &d->state) ||
      !df_edit_set(&d->edit, 1, DF_ATTR_DOCUMENT_STATE,
                   df_bytes_value(d->state.data, d->state.size)))
    return fail(d, "Cannot encode document");
  if (df_edit_apply(d->own, &d->edit) != DF_OK)
    return fail(d, df_store_error(d->own));
  ++d->revision;
  return true;
}
bool document_merge(document *d, const void *data, size_t size) {
  bool changed;
  if (!sequence_merge(&d->sequence, data, size, &changed))
    return fail(d, "Invalid CRDT state");
  return !changed || publish(d);
}
static bool restore(document *d, df_store *store) {
  df_slice value, bytes;
  df_status status = df_store_get(store, 1, DF_ATTR_DOCUMENT_STATE, &value);
  if (status == DF_ABSENT)
    return true;
  if (status != DF_OK || df_value_bytes(value, &bytes) != DF_OK)
    return fail(d, "Cannot read saved document");
  bool changed;
  return sequence_merge(&d->sequence, bytes.data, bytes.size, &changed) ||
         fail(d, "Invalid saved CRDT state");
}
static void receive(void *context, df_view *view) {
  document *d = context;
#ifdef CRDT_DEMO_TIMING
  uint64_t received = text_trace_clock(), before = d->revision;
#endif
  df_slice value, bytes;
  df_status status = df_view_get(view, 1, DF_ATTR_DOCUMENT_STATE, &value);
  /* An absent namespace is not a document deletion. Empty text is a CRDT edit. */
  if (status == DF_ABSENT || d->error[0])
    return;
  if (status != DF_OK || df_value_bytes(value, &bytes) != DF_OK) {
    fail(d, "Invalid peer document record");
    return;
  }
  document_merge(d, bytes.data, bytes.size);
#ifdef CRDT_DEMO_TIMING
  if (before != d->revision) {
    uint32_t tag = text_trace_tag(ct_bytes_string(&d->sequence.text));
    text_trace_record(tag, TEXT_TRACE_RECEIVE, received);
    text_trace_record(tag, TEXT_TRACE_MERGED, text_trace_clock());
  }
#endif
}
bool document_open(document *d, const char *directory, void *identity, const char *peer,
                   const char *host, uint16_t port) {
  const uint8_t *key = df_identity_public(identity);
  if (!key || !sequence_open(&d->sequence))
    return fail(d, "Cannot open document identity");
  char owner[65], path[4096];
  for (size_t i = 0; i < 32; ++i)
    snprintf(owner + i * 2, 3, "%02x", key[i]);
  if (strlen(directory) + 80 >= sizeof(path))
    return fail(d, "Document path too long");
  if (peer[0] &&
      (strlen(peer) != 64 || strspn(peer, "0123456789abcdef") != 64 || !strcmp(peer, owner)))
    return fail(d, "Invalid peer public key");
  df_buffer error = {0};
  snprintf(path, sizeof(path), "%s/own", directory);
  df_options options = {path, owner, DF_ASYNC, 64u << 20};
  d->own = df_store_open(&options, &error);
  if (!d->own)
    goto failed;
  if (!restore(d, d->own))
    goto failed;
  if (peer[0]) {
    snprintf(path, sizeof(path), "%s/peer-%s", directory, peer);
    options.namespace_name = peer;
    d->peer = df_store_open(&options, &error);
    if (!d->peer || !restore(d, d->peer))
      goto failed;
  }
  if (!publish(d))
    goto failed;
  if (d->peer) {
    uint32_t attribute = DF_ATTR_DOCUMENT_STATE;
    d->watch = df_watch_entity(d->peer, 1, &attribute, 1, receive, d);
    if (!d->watch || d->error[0])
      goto failed;
  }
  if (host[0]) {
    d->network = df_network_open(host, port, &error);
    if (!d->network || !df_network_push(d->network, d->own, identity, &error) ||
        (d->peer && !df_network_follow(d->network, d->peer, &error)))
      goto failed;
  }
  df_free(error.data);
  return true;
failed:
  if (!d->error[0])
    snprintf(d->error, sizeof(d->error), "%.*s", (int)(error.size < 511 ? error.size : 511),
             error.data ? (const char *)error.data : "");
  if (!d->error[0])
    fail(d, "Cannot open document store");
  df_free(error.data);
  return false;
}
bool document_edit(document *d, document_sequence *displayed, const char *text) {
  bool changed;
  ct_bytes state = {0};
  bool ok = sequence_replace(displayed, text, &changed);
  if (ok && changed)
    ok = sequence_state(displayed, &state) && document_merge(d, state.data, state.size);
  ct_bytes_free(&state);
  return ok || fail(d, "Cannot save document edit");
}
bool document_flush(document *d) {
  bool ok = !d->own || df_store_flush(d->own) == DF_OK;
  if (d->peer && df_store_flush(d->peer) != DF_OK)
    ok = false;
  return ok || fail(d, "Cannot flush document");
}
void document_close(document *d) {
  df_network_close(d->network);
  if (d->peer)
    df_watch_remove(d->peer, &d->watch);
  df_buffer error = {0};
  if (d->peer)
    df_store_close(d->peer, &error);
  df_free(error.data);
  error = (df_buffer){0};
  if (d->own)
    df_store_close(d->own, &error);
  df_free(error.data);
  df_edit_free(&d->edit);
  sequence_close(&d->sequence);
  ct_bytes_free(&d->state);
  memset(d, 0, sizeof(*d));
}
