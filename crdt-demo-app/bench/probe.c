#define _POSIX_C_SOURCE 200809L
#include "probe.h"
#include "text_buffers/trace.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define EXPORT __attribute__((visibility("default"), used))
typedef struct {
  uint64_t ns;
  uint32_t tag, stage;
} probe_event;
static probe_event events[65536];
static size_t count, overflow;
static uint32_t latest;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bench_callback notify;
EXPORT uint64_t text_trace_clock(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000000000 + (uint64_t)now.tv_nsec;
}
EXPORT uint32_t text_trace_tag(const char *text) {
  if (strlen(text) < 8 || text[0] != 'L' || text[1] != 'B')
    return 0;
  uint32_t tag = 0;
  for (int i = 2; i < 8; ++i) {
    if (text[i] < '0' || text[i] > '9')
      return 0;
    tag = tag * 10 + (unsigned)(text[i] - '0');
  }
  return tag;
}
EXPORT void text_trace_record(uint32_t tag, unsigned stage, uint64_t ns) {
  if (!tag)
    return;
  pthread_mutex_lock(&lock);
  if (count < sizeof(events) / sizeof(events[0]))
    events[count++] = (probe_event){ns, tag, stage};
  else
    ++overflow;
  pthread_mutex_unlock(&lock);
}
EXPORT int bench_save(const char *path) {
  FILE *file = fopen(path, "w");
  if (!file)
    return errno;
  pthread_mutex_lock(&lock);
  fprintf(file, "# overflow=%zu\ntag,stage,ns\n", overflow);
  for (size_t i = 0; i < count; ++i)
    fprintf(file, "%u,%u,%llu\n", events[i].tag, events[i].stage, (unsigned long long)events[i].ns);
  pthread_mutex_unlock(&lock);
  return fclose(file);
}
static void *produce(void *unused) {
  (void)unused;
  uint64_t start = text_trace_clock() + 15000000000ULL;
  for (unsigned i = 1; i <= 60; ++i) {
    uint64_t target = start + (i - 1) * 503000000ULL;
    for (;;) {
      uint64_t now = text_trace_clock();
      if (now >= target)
        break;
      uint64_t wait = target - now;
      struct timespec delay = {(time_t)(wait / 1000000000), (long)(wait % 1000000000)};
      nanosleep(&delay, NULL);
    }
    uint32_t tag = 300000 + i;
    text_trace_record(tag, 9, text_trace_clock());
    pthread_mutex_lock(&lock);
    latest = tag;
    pthread_mutex_unlock(&lock);
    if (notify)
      notify(tag);
  }
  return NULL;
}
EXPORT int bench_start(bench_callback callback) {
  notify = callback;
  pthread_t worker;
  int error = pthread_create(&worker, NULL, produce, NULL);
  if (!error)
    pthread_detach(worker);
  return error;
}
EXPORT uint32_t bench_latest(void) {
  pthread_mutex_lock(&lock);
  uint32_t value = latest;
  pthread_mutex_unlock(&lock);
  return value;
}
