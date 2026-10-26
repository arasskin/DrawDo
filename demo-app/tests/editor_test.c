#include "text_buffers/native.h"
#include "test.h"
/* Deterministic fake native UI thread with a separately queued C command. */
static editor_snapshot mailbox;
static text_buffer native;
static uint64_t generation, revision;
static bool ready, command;
static unsigned closes;
static uint64_t expected;
static app_buffer proposed;
static size_t command_anchor, command_caret;
static void emit(unsigned flags) {
  unsigned previous = ready && mailbox.id == generation ? mailbox.events : 0;
  CHECK(app_string(&mailbox.text, app_cstr(&native.bytes), native.bytes.size));
  mailbox.id = generation;
  mailbox.revision = ++revision;
  mailbox.events = flags | previous;
  size_t a, c;
  CHECK(text_to_utf16(app_cstr(&native.bytes), native.bytes.size, native.anchor, &a));
  CHECK(text_to_utf16(app_cstr(&native.bytes), native.bytes.size, native.caret, &c));
  mailbox.anchor = (int64_t)a;
  mailbox.caret = (int64_t)c;
  mailbox.marked_start = mailbox.marked_end = -1;
  if (native.composing) {
    CHECK(text_to_utf16(app_cstr(&native.bytes), native.bytes.size, native.marked_start, &a));
    CHECK(text_to_utf16(app_cstr(&native.bytes), native.bytes.size, native.marked_end, &c));
    mailbox.marked_start = (int64_t)a;
    mailbox.marked_end = (int64_t)c;
  }
  ready = true;
}
bool platform_editor_open(uint64_t id, const char *text, editor_options options) {
  (void)options;
  generation = id;
  revision = 0;
  command = false;
  CHECK(text_buffer_set(&native, text, strlen(text)));
  emit(0);
  return true;
}
bool platform_editor_poll(editor_snapshot *out) {
  if (!ready)
    return false;
  *out = mailbox;
  memset(&mailbox, 0, sizeof(mailbox));
  ready = false;
  return true;
}
bool platform_editor_set(uint64_t id, uint64_t rev, const char *text, size_t a, size_t c) {
  CHECK(id == generation && !command);
  CHECK(app_string(&proposed, text, strlen(text)));
  expected = rev;
  command_anchor = a;
  command_caret = c;
  command = true;
  return true;
}
bool platform_editor_layout(uint64_t id, editor_rect rect) {
  (void)rect;
  return id == generation;
}
void platform_editor_close(uint64_t id) {
  ++closes;
  if (id == generation)
    emit(EDITOR_CANCELLED);
}
static void execute(void) {
  CHECK(command);
  command = false;
  if (expected != revision || native.composing) {
    emit(EDITOR_ACK | EDITOR_REJECTED);
    return;
  }
  CHECK(text_buffer_set(&native, app_cstr(&proposed), proposed.size));
  size_t a, c;
  CHECK(text_from_utf16(app_cstr(&native.bytes), native.bytes.size, command_anchor, &a));
  CHECK(text_from_utf16(app_cstr(&native.bytes), native.bytes.size, command_caret, &c));
  CHECK(text_buffer_select(&native, a, c));
  emit(EDITOR_ACK);
}
static unsigned calls;
static app_buffer delivered;
static void edited(void *ctx, const char *text) {
  CHECK(ctx == &calls);
  ++calls;
  CHECK(app_string(&delivered, text, strlen(text)));
}
int main(void) {
  editor_session s = {0};
  editor_options options = {{0, 0, 100, 30}, 16,    0xffffffff, 0x000000ff, false, false,
                            "test",          false, 0,          0,          false, false};
  CHECK(editor_open(&s, "a🌍b", options, edited, &calls));
  editor_poll(&s);
  CHECK(s.buffer.caret == 6 && calls == 0);
  CHECK(editor_move_to(&s, 1, false) && s.pending);
  CHECK(!editor_move_to(&s, 0, false));
  execute();
  editor_poll(&s);
  CHECK(!s.pending && (s.events & EDITOR_ACK) && s.buffer.caret == 1 && calls == 0);
  CHECK(text_buffer_move_to(&native, 5, true));
  emit(0);
  editor_poll(&s);
  CHECK((s.events & EDITOR_SELECTION) && s.buffer.anchor == 1 && s.buffer.caret == 5 && !calls);
  CHECK(editor_replace(&s, 1, 5, "🌱", 4));
  execute();
  editor_poll(&s);
  CHECK(!strcmp(app_cstr(&s.buffer.bytes), "a🌱b") && calls == 0);
  /* Native input races with a queued C edit: latest typing wins. */
  CHECK(editor_set(&s, "stale", 5, 5, 5));
  CHECK(text_buffer_set(&native, "new typing", 10));
  emit(0);
  execute();
  editor_poll(&s);
  CHECK((s.events & EDITOR_REJECTED) && !s.pending &&
        !strcmp(app_cstr(&s.buffer.bytes), "new typing"));
  emit(EDITOR_SUBMITTED);
  editor_poll(&s);
  CHECK(!s.active && calls == 1 && !strcmp(app_cstr(&delivered), "new typing"));
  uint64_t old_generation = s.id;
  options.multiline = options.live = true;
  CHECK(editor_open(&s, "", options, edited, &calls));
  editor_poll(&s);
  CHECK(text_buffer_set(&native, "に", strlen("に")));
  CHECK(text_buffer_mark(&native, 0, native.bytes.size, true));
  emit(0);
  editor_poll(&s);
  CHECK(s.buffer.composing && calls == 1 && !editor_move_to(&s, 0, false));
  /* Composition can finish without changing the bytes. It must still publish. */
  CHECK(text_buffer_mark(&native, 0, 0, false));
  emit(0);
  editor_poll(&s);
  CHECK(!s.buffer.composing && calls == 2 && !strcmp(app_cstr(&delivered), "に"));
  CHECK(editor_move_to(&s, 0, false));
  execute();
  CHECK(text_buffer_insert(&native, "x", 1));
  emit(0);
  editor_poll(&s);
  CHECK(!s.pending && (s.events & EDITOR_ACK) && calls == 3);
  editor_snapshot stale = {0};
  stale.id = old_generation;
  stale.revision = UINT64_MAX;
  CHECK(!editor_receive(&s, &stale) && s.active);
  stale.id = s.id;
  stale.revision = s.native_revision;
  CHECK(!editor_receive(&s, &stale));
  CHECK(editor_layout(&s, (editor_rect){1, 2, 100, 30}));
  editor_cancel(&s);
  editor_poll(&s);
  CHECK(!s.active && calls == 3);
  options.live = false;
  CHECK(editor_open(&s, "unchanged", options, edited, &calls));
  editor_poll(&s);
  CHECK(text_buffer_set(&native, "discard", 7));
  emit(EDITOR_CANCELLED);
  editor_poll(&s);
  CHECK(!s.active && calls == 3 && (s.events & EDITOR_CANCELLED));
  CHECK(editor_open(&s, "preserve", options, edited, &calls));
  editor_poll(&s);
  emit(0);
  mailbox.anchor = 999;
  editor_poll(&s);
  CHECK(!s.active && !s.pending && (s.events & EDITOR_ERROR));
  CHECK(!strcmp(app_cstr(&s.buffer.bytes), "preserve") && calls == 3);
  options.retain_on_submit = true;
  CHECK(editor_open(&s, "before", options, edited, &calls));
  editor_poll(&s);
  CHECK(text_buffer_set(&native, "saved", 5));
  unsigned before = closes;
  emit(EDITOR_SUBMITTED);
  editor_poll(&s);
  CHECK(!s.active && s.retained && closes == before && calls == 4);
  /* A repeated Done/late native snapshot must not commit twice or release. */
  emit(EDITOR_SUBMITTED);
  editor_poll(&s);
  CHECK(s.retained && closes == before && calls == 4);
  editor_release(&s);
  CHECK(!s.retained && closes == before + 1);
  editor_release(&s);
  CHECK(closes == before + 1);
  CHECK(editor_open(&s, "again", options, edited, &calls));
  editor_poll(&s);
  emit(EDITOR_SUBMITTED);
  editor_poll(&s);
  CHECK(s.retained);
  before = closes;
  editor_cancel(&s);
  CHECK(!s.retained && closes == before + 1);
  options.live = true;
  CHECK(editor_open(&s, "live", options, edited, &calls));
  CHECK(!s.options.retain_on_submit);
  editor_free(&s);
  text_buffer_free(&native);
  app_buffer_free(&proposed);
  app_buffer_free(&delivered);
  app_buffer_free(&mailbox.text);
  puts("PASS editor: selection API, async ACK/rejection, stale events, composition, submit/cancel");
  return 0;
}
