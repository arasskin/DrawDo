#ifndef DRAWDO_PRESENT_H
#define DRAWDO_PRESENT_H
#include <stdbool.h>
#include <stdint.h>
/* One replacement frame at a time. Used only during editor-to-canvas handoff. */
typedef struct presentation {
  uint64_t frame;
  void *native;
  bool armed, fallback;
} presentation;
void present_track(presentation *, const void *drawable);
bool present_ready(presentation *);
void present_reset(presentation *);
#endif
