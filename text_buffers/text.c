#include "text_buffers/text.h"
static bool decode(const char *s, size_t n, size_t *at, uint32_t *value) {
  if (*at >= n)
    return false;
  unsigned char c = (unsigned char)s[*at];
  size_t width = c < 128                  ? 1
                 : c >= 0xc2 && c <= 0xdf ? 2
                 : c >= 0xe0 && c <= 0xef ? 3
                 : c >= 0xf0 && c <= 0xf4 ? 4
                                          : 0;
  if (!width || width > n - *at)
    return false;
  uint32_t cp = width == 1 ? c : c & ((1u << (7 - width)) - 1);
  for (size_t j = 1; j < width; ++j) {
    unsigned char next = (unsigned char)s[*at + j];
    if ((next & 192) != 128)
      return false;
    cp = (cp << 6) | (next & 63);
  }
  /* C-string app/native boundaries intentionally exclude embedded NUL. */
  if (!cp || (width == 2 && cp < 128) || (width == 3 && cp < 2048) || (width == 4 && cp < 65536) ||
      cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
    return false;
  *at += width;
  *value = cp;
  return true;
}
bool text_valid(const char *s, size_t n) {
  if (n && !s)
    return false;
  size_t at = 0;
  uint32_t cp;
  while (at < n)
    if (!decode(s, n, &at, &cp))
      return false;
  return true;
}
bool text_boundary(const char *s, size_t n, size_t byte) {
  return byte <= n && (!byte || byte == n || (s && ((unsigned char)s[byte] & 192) != 128));
}
bool text_to_utf16(const char *s, size_t n, size_t byte, size_t *units) {
  if (byte > n || (n && !s))
    return false;
  size_t at = 0, count = 0;
  uint32_t cp;
  while (at < byte) {
    if (!decode(s, n, &at, &cp) || at > byte)
      return false;
    count += cp > 0xffff ? 2 : 1;
  }
  *units = count;
  return true;
}
bool text_from_utf16(const char *s, size_t n, size_t units, size_t *byte) {
  if (n && !s)
    return false;
  size_t at = 0, count = 0;
  uint32_t cp;
  while (count < units) {
    if (!decode(s, n, &at, &cp))
      return false;
    count += cp > 0xffff ? 2 : 1;
    if (count > units)
      return false;
  }
  *byte = at;
  return true;
}
bool text_buffer_set(text_buffer *b, const char *s, size_t n) {
  if (n == SIZE_MAX || !text_valid(s, n) ||
      !ct_array_reserve(&b->bytes.data, &b->bytes.capacity, n + 1, 1))
    return false;
  if (n)
    memcpy(b->bytes.data, s, n);
  b->bytes.data[n] = 0;
  b->bytes.size = n;
  b->anchor = b->caret = n;
  b->anchor_affinity = b->caret_affinity = TEXT_AFTER;
  b->composing = false;
  b->marked_start = b->marked_end = 0;
  ++b->revision;
  return true;
}
bool text_buffer_select(text_buffer *b, size_t anchor, size_t caret) {
  const char *s = ct_bytes_string(&b->bytes);
  if (!text_boundary(s, b->bytes.size, anchor) || !text_boundary(s, b->bytes.size, caret))
    return false;
  if (b->anchor != anchor || b->caret != caret)
    ++b->revision;
  b->anchor = anchor;
  b->caret = caret;
  b->anchor_affinity = anchor < caret ? TEXT_BEFORE : TEXT_AFTER;
  b->caret_affinity = caret < anchor ? TEXT_BEFORE : TEXT_AFTER;
  return true;
}
bool text_buffer_move_to(text_buffer *b, size_t byte, bool extend) {
  return text_buffer_select(b, extend ? b->anchor : byte, byte);
}
bool text_buffer_mark(text_buffer *b, size_t start, size_t end, bool composing) {
  if (composing &&
      (start > end || !text_boundary(ct_bytes_string(&b->bytes), b->bytes.size, start) ||
       !text_boundary(ct_bytes_string(&b->bytes), b->bytes.size, end)))
    return false;
  if (b->composing != composing ||
      (composing && (start != b->marked_start || end != b->marked_end)))
    ++b->revision;
  b->composing = composing;
  b->marked_start = composing ? start : 0;
  b->marked_end = composing ? end : 0;
  return true;
}
static size_t remap(size_t position, text_affinity affinity, size_t start, size_t end,
                    size_t inserted) {
  if (position < start)
    return position;
  if (position > end)
    return position - (end - start) + inserted;
  if (position == end && end != start)
    return start + inserted;
  return start + (affinity == TEXT_AFTER ? inserted : 0);
}
bool text_buffer_replace(text_buffer *b, size_t start, size_t end, const char *s, size_t n) {
  size_t old = b->bytes.size;
  if (b->composing || start > end || !text_boundary(ct_bytes_string(&b->bytes), old, start) ||
      !text_boundary(ct_bytes_string(&b->bytes), old, end) ||
      n >= SIZE_MAX - (old - (end - start)) || !text_valid(s, n))
    return false;
  size_t size = old - (end - start) + n;
  if (!ct_array_reserve(&b->bytes.data, &b->bytes.capacity, size + 1, 1))
    return false;
  memmove(b->bytes.data + start + n, b->bytes.data + end, old - end);
  if (n)
    memcpy(b->bytes.data + start, s, n);
  b->bytes.data[size] = 0;
  b->bytes.size = size;
  b->anchor = remap(b->anchor, b->anchor_affinity, start, end, n);
  b->caret = remap(b->caret, b->caret_affinity, start, end, n);
  ++b->revision;
  return true;
}
bool text_buffer_insert(text_buffer *b, const char *s, size_t n) {
  size_t start = b->anchor < b->caret ? b->anchor : b->caret;
  size_t end = b->anchor > b->caret ? b->anchor : b->caret;
  return text_buffer_replace(b, start, end, s, n) && text_buffer_move_to(b, start + n, false);
}
void text_buffer_free(text_buffer *b) {
  ct_bytes_free(&b->bytes);
  memset(b, 0, sizeof(*b));
}
