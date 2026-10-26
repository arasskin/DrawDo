#include "text_crdt/crdt.h"
bool utf8_offsets(const char *text, size_t size, ct_bytes *out) {
  out->size = 0;
  for (size_t i = 0; i < size;) {
    size_t offset = i;
    unsigned char c = (unsigned char)text[i];
    unsigned n = c < 128                  ? 1
                 : c >= 0xc2 && c <= 0xdf ? 2
                 : c >= 0xe0 && c <= 0xef ? 3
                 : c >= 0xf0 && c <= 0xf4 ? 4
                                          : 0;
    if (!n || i + n > size)
      return false;
    uint32_t cp = n == 1 ? c : c & ((1u << (7 - n)) - 1);
    for (unsigned j = 1; j < n; j++) {
      unsigned char b = (unsigned char)text[i + j];
      if ((b & 192) != 128)
        return false;
      cp = (cp << 6) | (b & 63);
    }
    if ((n == 2 && cp < 128) || (n == 3 && cp < 2048) || (n == 4 && cp < 65536) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff))
      return false;
    if (!ct_bytes_append(out, &offset, sizeof(offset)))
      return false;
    i += n;
  }
  return ct_bytes_append(out, &size, sizeof(size));
}
static void changed(void *ctx, uint32_t n, const char *bytes) {
  (void)n;
  (void)bytes;
  ((document_sequence *)ctx)->changed = true;
}
bool sequence_open(document_sequence *s) {
  memset(s, 0, sizeof(*s));
  YOptions options = yoptions();
  options.encoding = Y_OFFSET_BYTES;
  s->doc = ydoc_new_with_options(options);
  /* yoptions allocates these strings; document construction copies them. */
  ystring_destroy((char *)options.guid);
  ystring_destroy((char *)options.collection_id);
  if (!s->doc)
    return false;
  s->text_type = ytext(s->doc, "document");
  s->updates = ydoc_observe_updates_v2(s->doc, s, changed);
  return s->text_type && s->updates;
}
void sequence_close(document_sequence *s) {
  if (s->updates)
    yunobserve(s->updates);
  if (s->doc)
    ydoc_destroy(s->doc);
  ct_bytes_free(&s->text);
  memset(s, 0, sizeof(*s));
}
static bool refresh(document_sequence *s) {
  YTransaction *tx = ydoc_read_transaction(s->doc);
  if (!tx)
    return false;
  uint32_t n = ytext_len(s->text_type, tx);
  char *raw = ytext_string(s->text_type, tx);
  bool ok = raw && ct_bytes_set(&s->text, raw, n);
  if (raw)
    ystring_destroy(raw);
  ytransaction_commit(tx);
  return ok;
}
bool sequence_state(document_sequence *s, ct_bytes *out) {
  YTransaction *tx = ydoc_read_transaction(s->doc);
  if (!tx)
    return false;
  uint32_t n = 0;
  char *raw = ytransaction_state_diff_v2(tx, NULL, 0, &n);
  bool ok = raw && ct_bytes_set(out, raw, n);
  if (raw)
    ybinary_destroy(raw, n);
  ytransaction_commit(tx);
  return ok;
}
bool sequence_merge(document_sequence *s, const void *data, size_t n, bool *modified) {
  *modified = false;
  if (n > UINT32_MAX)
    return false;
  s->changed = false;
  YTransaction *tx = ydoc_write_transaction(s->doc, 0, NULL);
  if (!tx)
    return false;
  bool ok = ytransaction_apply_v2(tx, data, (uint32_t)n) == 0;
  ytransaction_commit(tx);
  if (!ok)
    return false;
  *modified = s->changed;
  return !s->changed || refresh(s);
}
bool sequence_replace(document_sequence *s, const char *next, bool *modified) {
  *modified = false;
  size_t n = strlen(next), old = s->text.size;
  if (n == old && !memcmp(next, ct_bytes_string(&s->text), n))
    return true;
  if (n > UINT32_MAX)
    return false;
  ct_bytes offsets = {0};
  bool valid = utf8_offsets(next, n, &offsets);
  ct_bytes_free(&offsets);
  if (!valid)
    return false;
  const char *before = ct_bytes_string(&s->text);
  size_t prefix = 0, suffix = 0;
  while (prefix < n && prefix < old && next[prefix] == before[prefix])
    prefix++;
  while (prefix < n && prefix && ((unsigned char)next[prefix] & 192) == 128)
    prefix--;
  while (suffix < n - prefix && suffix < old - prefix &&
         next[n - 1 - suffix] == before[old - 1 - suffix])
    suffix++;
  while (suffix && ((unsigned char)next[n - suffix] & 192) == 128)
    suffix--;
  ct_bytes insert = {0};
  if (!ct_bytes_set(&insert, next + prefix, n - prefix - suffix))
    return false;
  YTransaction *tx = ydoc_write_transaction(s->doc, 0, NULL);
  if (!tx) {
    ct_bytes_free(&insert);
    return false;
  }
  size_t removed = old - prefix - suffix;
  if (removed)
    ytext_remove_range(s->text_type, tx, (uint32_t)prefix, (uint32_t)removed);
  if (insert.size)
    ytext_insert(s->text_type, tx, (uint32_t)prefix, ct_bytes_string(&insert), NULL);
  ytransaction_commit(tx);
  ct_bytes_free(&insert);
  *modified = true;
  return refresh(s);
}
bool sequence_fork(document_sequence *from, document_sequence *to) {
  ct_bytes state = {0};
  bool changed_value = false;
  if (!sequence_open(to))
    return false;
  bool ok =
      sequence_state(from, &state) && sequence_merge(to, state.data, state.size, &changed_value);
  ct_bytes_free(&state);
  if (!ok)
    sequence_close(to);
  return ok;
}
