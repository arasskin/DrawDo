#ifndef APP_METRICS_H
#define APP_METRICS_H
#include "base.h"
typedef struct profile_metrics {
  uint64_t texture_bytes, texture_entries;
  int64_t encode, engine, publish, subscribe, unsubscribe, text, raster, upload;
  int64_t geometry_upload, shape_submit, label_upload, label_submit, commit, diagnostics,
      pass_begin, shape_draw;
  int64_t jni_attach, jni_detach, java_raster, bitmap_copy;
  int jni_attachments, edits, subscriptions, unsubscriptions, text_misses, labels;
} profile_metrics;
extern bool profile_enabled;
extern profile_metrics profile_current;
static inline int64_t profile_start(void) { return profile_enabled ? app_now() : 0; }
static inline void profile_end(int64_t *total, int64_t start) {
  if (start)
    *total += app_now() - start;
}
#endif
