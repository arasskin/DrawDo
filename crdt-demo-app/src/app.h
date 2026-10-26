#ifndef CRDT_DEMO_APP_H
#define CRDT_DEMO_APP_H
#include "document.h"
#include "text_buffers/editor.h"
typedef struct app {
  document document;
  document_sequence displayed;
  editor_session editor;
  void *identity;
  ct_bytes refresh;
  bool refreshing, adopted;
  uint64_t displayed_revision, refresh_revision;
  char error[512];
} app;
bool app_open(app *, const char *directory, editor_rect, float font_pixels);
bool app_step(app *, editor_rect);
void app_pause(app *);
void app_close(app *);
#endif
