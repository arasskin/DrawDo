#ifndef APP_DOCUMENT_SCREEN_H
#define APP_DOCUMENT_SCREEN_H
#include "document_model.h"
#include "ui/ui.h"
typedef struct document_screen {
  document_model *model;
  ui_editor edit_text;
  editor_session *session;
  ui_rect edit_bounds;
  document_sequence draft;
  bool drafting;
  app_buffer status;
  int64_t status_at;
  ui_rect hits[5];
} document_screen;
void document_screen_init(document_screen *, document_model *);
void document_screen_free(document_screen *);
void document_screen_draw(document_screen *, ui_surface *);
void document_screen_press(document_screen *, float, float);
#endif
