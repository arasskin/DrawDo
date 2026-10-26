#ifndef APP_PRESENTATION_H
#define APP_PRESENTATION_H
#include "base.h"
typedef struct presentation_stamp {
  uint64_t id;
  int64_t queue, gpu_done, latch, present;
  void *native;
} presentation_stamp;
bool presentation_init(void);
void presentation_begin(presentation_stamp *);
void presentation_track(presentation_stamp *, const void *);
void presentation_collect(presentation_stamp *);
void presentation_release(presentation_stamp *);
#endif
