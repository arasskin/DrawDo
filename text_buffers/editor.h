#ifndef APP_UI_EDITOR_H
#define APP_UI_EDITOR_H
#include "text_buffers/text.h"
typedef struct editor_rect {
  float x, y, w, h;
} editor_rect;
typedef struct editor_options {
  editor_rect bounds; /* Platform pixels; caller owns layout. */
  float font_pixels;
  uint32_t foreground, background; /* RGBA; inline overlays use an opaque background. */
  bool multiline, live;
  const char *label;
  bool place_at_point;
  float point_x, point_y; /* Initial hit test, pixels relative to bounds origin. */
  bool embedded;          /* Persistent native view; tapping focuses it, blur leaves it visible. */
  bool retain_on_submit;  /* Non-live overlay: freeze on Done until editor_release(). */
} editor_options;
enum editor_events {
  EDITOR_TEXT = 1,
  EDITOR_SELECTION = 2,
  EDITOR_COMPOSITION = 4,
  EDITOR_SUBMITTED = 8,
  EDITOR_CANCELLED = 16,
  EDITOR_ACK = 32,
  EDITOR_REJECTED = 64,
  EDITOR_ERROR = 128
};
typedef void (*editor_callback)(void *, const char *);
/* App-owned, zero-init. Only native mailbox delivery is cross-thread.
 * One active session per application. Screens may borrow this session.
 * Poll flags last until the next poll. Callback text is borrowed for the call.
 * Callback must not reopen/poll/destroy this session recursively. */
typedef struct editor_session {
  text_buffer buffer;
  ct_bytes delivered;
  editor_options options;
  uint64_t id, native_revision;
  unsigned events;
  bool active, pending, retained;
  editor_callback callback;
  void *context;
} editor_session;
bool editor_open(editor_session *, const char *, editor_options, editor_callback, void *);
void editor_poll(editor_session *);
void editor_cancel(editor_session *);
void editor_free(editor_session *);
/* Call after the replacement scene is presented. Cancel/free also release it. */
void editor_release(editor_session *);
bool editor_layout(editor_session *, editor_rect);
/* Commands are acknowledged asynchronously by poll. Rejected while composing
 * or a command is outstanding. A race with newer native input sets REJECTED;
 * poll restores the latest native snapshot, never overwrites newer typing.
 * Caller can retry/rebase against that snapshot. UTF-8 byte positions. */
bool editor_set(editor_session *, const char *, size_t, size_t anchor, size_t caret);
bool editor_select(editor_session *, size_t anchor, size_t caret);
bool editor_move_to(editor_session *, size_t byte, bool extend);
bool editor_replace(editor_session *, size_t start, size_t end, const char *, size_t);
#endif
