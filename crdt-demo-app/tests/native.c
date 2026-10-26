/* Run against the real platform adapter in an isolated timing build (run.txt=2).
 * No network pumping or document edits; each assertion uses a native ACK. */
#include "app.h"
#include "text_buffers/native.h"
#include "text_buffers/trace.h"
#include <stdio.h>

static const char *cases[] = {"",
                              "A😀Z",
                              "A😁Z",
                              "A🈀Z",
                              "A𐈀Z",
                              "Ae\xcc\x81Z",
                              "AéZ",
                              "مرحبا بالعالم",
                              "first\nsecond",
                              "first\nchanged\nsecond",
                              "a👩‍🚀z",
                              "a👩‍🔬z",
                              "a👩‍🔬z",
                              "",
                              "end"};
static size_t index_value, caret, retries;
static bool sent, done, stale;
static uint64_t began;

static void result(const char *directory, const char *message) {
  char path[4200];
  snprintf(path, sizeof(path), "%s/native-check.txt", directory);
  FILE *file = fopen(path, "w");
  if (file) {
    fprintf(file, "%s (case %zu, retries %zu)\n", message, index_value, retries);
    fclose(file);
  }
  done = true;
}

void native_check_tick(app *a, const char *directory) {
  if (done)
    return;
  if (!began)
    began = text_trace_clock();
  if (text_trace_clock() - began > UINT64_C(15000000000)) {
    result(directory, "FAIL: native ACK timeout");
    return;
  }
  editor_session *s = &a->editor;
  s->callback = NULL;
  s->context = NULL;
  editor_poll(s);
  if (s->events & EDITOR_ERROR) {
    result(directory, "FAIL: invalid native snapshot");
    return;
  }
  if (sent) {
    if (!(s->events & EDITOR_ACK))
      return;
    if (stale) {
      result(directory,
             (s->events & EDITOR_REJECTED) && !strcmp(ct_bytes_string(&s->buffer.bytes), "end")
                 ? "PASS: Unicode range updates, selection, empty text, stale rejection"
                 : "FAIL: stale native update was not rejected");
      return;
    }
    /* Android may deliver a late native selection revision during startup.
     * Retry a rejected command against that snapshot, just as the app does. */
    if (s->events & EDITOR_REJECTED) {
      ++retries;
    } else if (strcmp(ct_bytes_string(&s->buffer.bytes), cases[index_value]) ||
               s->buffer.anchor != caret || s->buffer.caret != caret) {
      char message[1024];
      snprintf(message, sizeof(message), "FAIL: expected '%s' at %zu, got '%s' at %zu/%zu flags=%u",
               cases[index_value], caret, ct_bytes_string(&s->buffer.bytes), s->buffer.anchor,
               s->buffer.caret, s->events);
      result(directory, message);
      return;
    } else {
      ++index_value;
    }
    sent = false;
  }
  if (!s->native_revision)
    return; /* Wait for the initial native snapshot. */
  if (index_value == sizeof(cases) / sizeof(cases[0])) {
    stale = true;
    sent = platform_editor_set(s->id, s->native_revision - 1, "stale", 0, 0);
  } else {
    size_t n = strlen(cases[index_value]);
    caret = index_value % 2 ? n : 0;
    sent = editor_set(s, cases[index_value], n, caret, caret);
  }
  if (!sent)
    result(directory, "FAIL: native command refused");
}
