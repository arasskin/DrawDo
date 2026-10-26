#ifndef APP_PLATFORM_TEXT_INPUT_H
#define APP_PLATFORM_TEXT_INPUT_H
#include "text_buffers/editor.h"
/* Internal native bridge. Positions use UTF-16 code units; -1 means no marked
 * range. Snapshots carry monotonic native revision and session generation.
 * A mailbox may coalesce snapshots, but must preserve ACK/REJECTED/end flags. */
typedef struct editor_snapshot {
  uint64_t id, revision;
  ct_bytes text;
  int64_t anchor, caret, marked_start, marked_end;
  unsigned events;
} editor_snapshot;
bool platform_editor_open(uint64_t, const char *, editor_options);
bool platform_editor_poll(editor_snapshot *);
/* Optional mailbox wake. Install/remove on the owner thread. Called by the
 * native producer; it must only signal the owner, never poll/reenter editing.
 * Passing NULL unregisters and waits for an in-flight notification on Android. */
void editor_native_notify(void (*wake)(void));
bool platform_editor_set(uint64_t id, uint64_t expected_revision, const char *, size_t anchor_utf16,
                         size_t caret_utf16);
bool platform_editor_layout(uint64_t, editor_rect);
void platform_editor_close(uint64_t);
/* Shared snapshot adoption is public to the bridge and host regression tests. */
bool editor_receive(editor_session *, const editor_snapshot *);
#if defined(__APPLE__)
void editor_apple_init(void *window);
#endif
#endif
