#ifndef APP_DOCUMENT_FRAMES_H
#define APP_DOCUMENT_FRAMES_H
#include "profiling/presentation.h"
typedef struct document_frame_sample {
  int64_t start, interval, cpu, total, work, steady_offset;
  uint64_t peer_tick;
  presentation_stamp stamp;
} document_frame_sample;
typedef struct document_frames {
  document_frame_sample current, *samples;
  size_t count, capacity;
  int64_t cpu_start, previous;
  bool active, failed;
} document_frames;
void document_frames_begin(document_frames *, bool);
void document_frames_work(document_frames *, uint64_t);
void document_frames_track(document_frames *, const void *);
void document_frames_end(document_frames *);
void document_frames_json(void *, FILE *);
void document_frames_free(document_frames *);
#endif
