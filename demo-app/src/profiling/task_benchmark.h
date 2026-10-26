#ifndef APP_TASK_BENCHMARK_H
#define APP_TASK_BENCHMARK_H
#include "profiling/metrics.h"
#include "profiling/presentation.h"
#include "task_screen.h"
typedef struct profile_row {
  int frame, action;
  int64_t start, interval, action_ns, prepare_ns, submit_ns, total_ns, cpu_ns;
  profile_metrics phases;
  presentation_stamp stamp;
  uint64_t list_builds, row_builds, counter_builds, detail_builds;
} profile_row;
typedef struct profile_session {
  profile_row *rows, current;
  size_t size, capacity;
  bool active, done, presentation;
  int count, kind, warmup, frames, frame;
  int64_t previous, cpu_start;
  app_buffer output;
  uint32_t first, added;
  uint64_t list_start, row_start, counter_start, detail_start;
} profile_session;
bool profile_configure(profile_session *, const char *);
bool profile_seed(profile_session *, task_model *, task_screen *);
bool profile_begin(profile_session *, task_model *, task_screen *);
void profile_track(profile_session *, const void *);
void profile_finish_frame(profile_session *, task_model *, int64_t, int64_t);
void profile_free(profile_session *);
#endif
