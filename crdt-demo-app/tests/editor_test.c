#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "text_buffers/native.h"
#include "../src/app.c"
#include <unistd.h>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                      \
      exit(1);                                                                                     \
    }                                                                                              \
  } while (0)
/* Deterministic fake native UI thread with a separately queued C command. */
static editor_snapshot mailbox;
static text_buffer native;
static uint64_t generation, revision;
static bool ready, command;
static uint64_t expected;
static ct_bytes proposed;
static size_t command_anchor, command_caret;
static void emit(unsigned flags) {
  unsigned previous = ready && mailbox.id == generation ? mailbox.events : 0;
  CHECK(ct_bytes_set(&mailbox.text, ct_bytes_string(&native.bytes), native.bytes.size));
  mailbox.id = generation;
  mailbox.revision = ++revision;
  mailbox.events = flags | previous;
  size_t a, c;
  CHECK(text_to_utf16(ct_bytes_string(&native.bytes), native.bytes.size, native.anchor, &a));
  CHECK(text_to_utf16(ct_bytes_string(&native.bytes), native.bytes.size, native.caret, &c));
  mailbox.anchor = (int64_t)a;
  mailbox.caret = (int64_t)c;
  mailbox.marked_start = mailbox.marked_end = -1;
  if (native.composing) {
    CHECK(
        text_to_utf16(ct_bytes_string(&native.bytes), native.bytes.size, native.marked_start, &a));
    CHECK(text_to_utf16(ct_bytes_string(&native.bytes), native.bytes.size, native.marked_end, &c));
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
  CHECK(ct_bytes_set(&proposed, text, strlen(text)));
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
  CHECK(text_buffer_set(&native, ct_bytes_string(&proposed), proposed.size));
  size_t a, c;
  CHECK(text_from_utf16(ct_bytes_string(&native.bytes), native.bytes.size, command_anchor, &a));
  CHECK(text_from_utf16(ct_bytes_string(&native.bytes), native.bytes.size, command_caret, &c));
  CHECK(text_buffer_select(&native, a, c));
  emit(EDITOR_ACK);
}
static void remote(app *a, document_sequence *peer, const char *text) {
  bool modified;
  ct_bytes state = {0};
  CHECK(sequence_replace(peer, text, &modified));
  CHECK(sequence_state(peer, &state));
  CHECK(document_merge(&a->document, state.data, state.size));
  ct_bytes_free(&state);
}
static void type(const char *text) {
  CHECK(text_buffer_set(&native, text, strlen(text)));
  emit(0);
}
int main(void) {
  char directory[] = "/tmp/crdt-editor-test-XXXXXX";
  CHECK(mkdtemp(directory));
  app a = {0};
  df_buffer error = {0};
  a.identity = df_identity_open(NULL, 0, 1, &error);
  CHECK(a.identity);
  df_free(error.data);
  CHECK(document_open(&a.document, directory, a.identity, "", "", 0));
  CHECK(sequence_fork(&a.document.sequence, &a.displayed));
  editor_options options = {.bounds = {0, 0, 300, 500}, .live = true, .embedded = true};
  CHECK(editor_open(&a.editor, "", options, edited, &a));
  CHECK(app_step(&a, options.bounds));
  type("A");
  CHECK(app_step(&a, options.bounds));
  document_sequence peer = {0};
  CHECK(sequence_fork(&a.document.sequence, &peer));
  remote(&a, &peer, "AB");
  CHECK(app_step(&a, options.bounds) && command && a.refreshing);
  type("AX"); // Typing wins the native revision race, but must preserve remote B.
  execute();
  CHECK(app_step(&a, options.bounds));
  CHECK(strstr(ct_bytes_string(&a.document.sequence.text), "B"));
  CHECK(strstr(ct_bytes_string(&a.document.sequence.text), "X"));
  CHECK(command);
  execute();
  CHECK(app_step(&a, options.bounds));
  CHECK(!a.refreshing);
  CHECK(!strcmp(ct_bytes_string(&native.bytes), ct_bytes_string(&a.document.sequence.text)));
  // ACK and subsequent user typing can be coalesced into the same mailbox entry.
  remote(&a, &peer, "ABC");
  CHECK(app_step(&a, options.bounds) && command);
  execute();
  ct_bytes typed = {0};
  CHECK(ct_bytes_set(&typed, ct_bytes_string(&native.bytes), native.bytes.size));
  CHECK(ct_bytes_append(&typed, "!", 1));
  type(ct_bytes_string(&typed));
  CHECK(app_step(&a, options.bounds));
  CHECK(!strcmp(ct_bytes_string(&a.document.sequence.text), ct_bytes_string(&typed)));
  // A suspended app must consume an outstanding ACK before its next poll.
  remote(&a, &peer, "ABCD");
  CHECK(app_step(&a, options.bounds) && command);
  execute();
  app_pause(&a);
  CHECK(!a.refreshing && !a.error[0]);
  CHECK(app_step(&a, options.bounds));
  // Composition defers a refresh until committed native text can be merged.
  text_buffer_mark(&native, 0, 1, true);
  emit(0);
  CHECK(app_step(&a, options.bounds));
  remote(&a, &peer, "ABCDE");
  CHECK(app_step(&a, options.bounds) && !command);
  text_buffer_mark(&native, 0, 0, false);
  emit(0);
  CHECK(app_step(&a, options.bounds) && command);
  execute();
  CHECK(app_step(&a, options.bounds));
  CHECK(!strcmp(ct_bytes_string(&native.bytes), ct_bytes_string(&a.document.sequence.text)));
  CHECK(strstr(ct_bytes_string(&native.bytes), "!"));
  sequence_close(&peer);
  app_close(&a);
  text_buffer_free(&native);
  ct_bytes_free(&typed);
  ct_bytes_free(&proposed);
  ct_bytes_free(&mailbox.text);
  puts("PASS: rejected refresh, coalesced ACK/typing, pause, composition");
  return 0;
}
