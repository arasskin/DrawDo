#ifndef DRAWDO_SCREEN_H
#define DRAWDO_SCREEN_H
#include "model.h"
#include "text_buffers/editor.h"
#include "accessible_ui/access.h"
typedef struct screen_item {
  uint32_t id; /* zero is the add row */
  editor_rect row, text;
} screen_item;
typedef struct todo_screen {
  screen_item items[128];
  size_t count;
  editor_rect bounds;
  float scale;
  access_tree accessibility;
} todo_screen;
bool screen_open(void);
bool screen_build(todo_screen *, todo_model *, editor_rect, float, bool editing);
void screen_draw(float width, float height);
const screen_item *screen_hit(todo_screen *, float x, float y, bool *checkbox);
void screen_close(todo_screen *);
#endif
