#define _POSIX_C_SOURCE 200809L
#include "timing.h"
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef __APPLE__
void timing_capture_start(const char *directory);
void timing_capture_stop(void);
#endif
#include "probe.h"
#include "loop.h"
static void produced(uint32_t tag) {
  (void)tag;
  loop_wake();
}
static char directory[4096];
static uint64_t start;
static int role = -1, index_value;
static bool saved;
static document_sequence writer;
void native_check_tick(app *, const char *directory);
const char *timing_directory(const char *base) {
  if (!base || strlen(base) + 16 >= sizeof(directory))
    return NULL;
  snprintf(directory, sizeof(directory), "%s/timing", base);
  if (mkdir(directory, 0700) && errno != EEXIST)
    return NULL;
  return directory;
}
void timing_start(const char *path) {
  (void)path;
  char file_path[4200];
  snprintf(file_path, sizeof(file_path), "%s/events.csv", directory);
  remove(file_path); /* A failed run must not look like a previous successful run. */
  snprintf(file_path, sizeof(file_path), "%s/run.txt", directory);
  FILE *file = fopen(file_path, "r");
  if (file) {
    if (fscanf(file, "%d", &role) != 1 || role < 0 || role > 3)
      role = -1;
    fclose(file);
  }
  start = text_trace_clock();
  if (role == 3)
    bench_start(produced);
#ifdef __APPLE__
  if (role >= 0 && role != 2)
    timing_capture_start(directory);
#endif
}
void timing_save(void) {
  if (!directory[0])
    return;
  char file_path[4200];
  snprintf(file_path, sizeof(file_path), "%s/events.csv", directory);
  bench_save(file_path);
}
bool timing_tick(app *a) {
  if (role == 3) {
    a->editor.callback = NULL;
    a->editor.context = NULL;
    editor_poll(&a->editor);
    uint32_t tag = bench_latest();
    if (tag && !a->editor.pending &&
        text_trace_tag(ct_bytes_string(&a->editor.buffer.bytes)) != tag) {
      char text[1001];
      snprintf(text, sizeof(text), "LB%06u\n", tag);
      for (size_t i = 9; i < 1000; ++i)
        text[i] = i % 51 == 0 ? '\n' : 'x';
      text[1000] = 0;
      text_trace_record(tag, 3, text_trace_clock());
      editor_set(&a->editor, text, 1000, 0, 0);
    }
    if (!saved && text_trace_clock() - start > 55000000000ULL) {
#ifdef __APPLE__
      timing_capture_stop();
#endif
      timing_save();
      saved = true;
    }
    return false;
  }
  if (role == 2) {
    native_check_tick(a, directory);
    return false;
  }
  if (role < 0 || saved)
    return true;
  uint64_t elapsed_ms = (text_trace_clock() - start) / 1000000;
  uint64_t begins = role ? 60000 : 20000;
  if (index_value < 60 && elapsed_ms >= begins + (uint64_t)index_value * 503) {
    if (!writer.doc && !sequence_fork(&a->document.sequence, &writer))
      return true;
    char text[1001];
    snprintf(text, sizeof(text), "LB%06u\n", (role ? 200000 : 100000) + ++index_value);
    for (size_t i = 9; i < 1000; ++i)
      text[i] = i % 51 == 0 ? '\n' : 'x';
    text[1000] = 0;
    if (!document_edit(&a->document, &writer, text))
      snprintf(a->error, sizeof(a->error), "%s", a->document.error);
  }
  if (elapsed_ms >= 100000) {
#ifdef __APPLE__
    timing_capture_stop();
#endif
    timing_save();
    saved = true;
    sequence_close(&writer);
  }
  return true;
}

uint64_t timing_timeout(void) {
  if (role < 0 || saved)
    return UINT64_MAX;
  if (role == 2)
    return 10000;
  uint64_t target;
  if (role == 3)
    target = start + 55000000001ULL;
  else if (index_value < 60)
    target =
        start + (role ? 60000000000ULL : 20000000000ULL) + (uint64_t)index_value * 503000000ULL;
  else
    target = start + 100000000000ULL;
  uint64_t now = text_trace_clock();
  return target > now ? (target - now + 999) / 1000 : 0;
}
