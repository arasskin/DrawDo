#include "text_buffers/native.h"
static bool equal(const ct_bytes *b, const char *s, size_t n) {
  return b->size == n && (!n || !memcmp(b->data, s, n));
}
bool editor_open(editor_session *s, const char *text, editor_options options, editor_callback cb,
                 void *ctx) {
  editor_cancel(s);
  options.retain_on_submit = options.retain_on_submit && !options.live && !options.embedded;
  if (!text_buffer_set(&s->buffer, text, strlen(text)) ||
      !ct_bytes_set(&s->delivered, text, strlen(text)))
    return false;
  s->options = options;
  s->callback = cb;
  s->context = ctx;
  s->native_revision = 0;
  s->events = 0;
  s->pending = false;
  s->active = platform_editor_open(s->id, text, options);
  if (!s->active) {
    s->callback = NULL;
    s->context = NULL;
    s->events = EDITOR_ERROR;
  }
  return s->active;
}
void editor_cancel(editor_session *s) {
  if (s->active || s->retained)
    platform_editor_close(s->id);
  ++s->id;
  s->active = s->pending = s->retained = false;
  s->callback = NULL;
  s->context = NULL;
  s->events = EDITOR_CANCELLED;
  text_buffer_mark(&s->buffer, 0, 0, false);
}
void editor_release(editor_session *s) {
  if (!s->retained)
    return;
  platform_editor_close(s->id);
  s->retained = false;
}
void editor_free(editor_session *s) {
  editor_cancel(s);
  text_buffer_free(&s->buffer);
  ct_bytes_free(&s->delivered);
  /* Preserve generation so late callbacks cannot match a reused session. */
}
bool editor_receive(editor_session *s, const editor_snapshot *event) {
  if (!s->active || event->id != s->id || event->revision <= s->native_revision)
    return false;
  const char *text = ct_bytes_string(&event->text);
  size_t n = event->text.size, anchor, caret, start = 0, end = 0;
  bool composing = event->marked_start >= 0 && event->marked_end >= 0;
  if (event->anchor < 0 || event->caret < 0 || !text_valid(text, n) ||
      !text_from_utf16(text, n, (size_t)event->anchor, &anchor) ||
      !text_from_utf16(text, n, (size_t)event->caret, &caret) ||
      (composing && (!text_from_utf16(text, n, (size_t)event->marked_start, &start) ||
                     !text_from_utf16(text, n, (size_t)event->marked_end, &end) || start > end))) {
    s->events |= EDITOR_ERROR;
    return false;
  }
  bool changed = !equal(&s->buffer.bytes, text, n);
  unsigned flags = event->events;
  if (changed)
    flags |= EDITOR_TEXT;
  if (s->buffer.anchor != anchor || s->buffer.caret != caret)
    flags |= EDITOR_SELECTION;
  if (s->buffer.composing != composing || s->buffer.marked_start != start ||
      s->buffer.marked_end != end)
    flags |= EDITOR_COMPOSITION;
  /* Reserve before adopting the event, so failure preserves the last accepted
   * contents. Poll terminates the session and reports EDITOR_ERROR on failure. */
  bool deliver =
      !(flags & EDITOR_CANCELLED) && !composing &&
      ((flags & EDITOR_SUBMITTED) || (s->options.live && !equal(&s->delivered, text, n)));
  if (deliver && !ct_array_reserve(&s->delivered.data, &s->delivered.capacity, n + 1, 1)) {
    s->events |= EDITOR_ERROR;
    return false;
  }
  if (changed && !text_buffer_set(&s->buffer, text, n)) {
    s->events |= EDITOR_ERROR;
    return false;
  }
  text_buffer_select(&s->buffer, anchor, caret);
  text_buffer_mark(&s->buffer, start, end, composing);
  s->native_revision = event->revision;
  s->events |= flags;
  if (flags & (EDITOR_ACK | EDITOR_REJECTED))
    s->pending = false;
  if (flags & (EDITOR_SUBMITTED | EDITOR_CANCELLED))
    s->active = s->pending = false;
  if (flags & EDITOR_SUBMITTED)
    s->retained = s->options.retain_on_submit && !(flags & EDITOR_CANCELLED);
  if (deliver) {
    ct_bytes_set(&s->delivered, text, n);
    if (s->callback)
      s->callback(s->context, ct_bytes_string(&s->buffer.bytes));
  }
  if (!s->active) {
    s->callback = NULL;
    s->context = NULL;
  }
  return true;
}
void editor_poll(editor_session *s) {
  s->events = 0;
  editor_snapshot event = {0};
  if (platform_editor_poll(&event)) {
    if (!editor_receive(s, &event) && (s->events & EDITOR_ERROR)) {
      editor_cancel(s);
      s->events |= EDITOR_ERROR;
    }
    ct_bytes_free(&event.text);
  }
}
bool editor_layout(editor_session *s, editor_rect r) {
  if (!s->active)
    return false;
  editor_rect old = s->options.bounds;
  if (old.x == r.x && old.y == r.y && old.w == r.w && old.h == r.h)
    return true;
  if (!platform_editor_layout(s->id, r))
    return false;
  s->options.bounds = r;
  return true;
}
bool editor_set(editor_session *s, const char *text, size_t n, size_t anchor, size_t caret) {
  size_t a, c;
  if (!s->active || s->pending || s->buffer.composing || n == SIZE_MAX || !text_valid(text, n) ||
      !text_to_utf16(text, n, anchor, &a) || !text_to_utf16(text, n, caret, &c))
    return false;
  /* Bridge accepts C strings; caller's span need not have a terminator. */
  ct_bytes copy = {0};
  bool ok = ct_bytes_set(&copy, text, n) &&
            platform_editor_set(s->id, s->native_revision, ct_bytes_string(&copy), a, c);
  ct_bytes_free(&copy);
  if (ok)
    s->pending = true;
  return ok;
}
bool editor_select(editor_session *s, size_t anchor, size_t caret) {
  return editor_set(s, ct_bytes_string(&s->buffer.bytes), s->buffer.bytes.size, anchor, caret);
}
bool editor_move_to(editor_session *s, size_t byte, bool extend) {
  return editor_select(s, extend ? s->buffer.anchor : byte, byte);
}
bool editor_replace(editor_session *s, size_t start, size_t end, const char *text, size_t n) {
  if (!s->active || s->pending || s->buffer.composing)
    return false;
  text_buffer next = {0};
  bool ok = text_buffer_set(&next, ct_bytes_string(&s->buffer.bytes), s->buffer.bytes.size) &&
            text_buffer_select(&next, s->buffer.anchor, s->buffer.caret);
  next.anchor_affinity = s->buffer.anchor_affinity;
  next.caret_affinity = s->buffer.caret_affinity;
  ok = ok && text_buffer_replace(&next, start, end, text, n) &&
       editor_set(s, ct_bytes_string(&next.bytes), next.bytes.size, next.anchor, next.caret);
  text_buffer_free(&next);
  return ok;
}
