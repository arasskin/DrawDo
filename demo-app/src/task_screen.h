#ifndef APP_TASK_SCREEN_H
#define APP_TASK_SCREEN_H
#include "task_model.h"
#include "ui/ui.h"
typedef struct task_hit {
  ui_rect bounds;
  int action;
  uint32_t id;
} task_hit;
typedef struct task_screen {
  task_model *model;
  ui_editor edit_text;
  editor_session *session; /* Borrowed from top-level interaction state. */
  ui_rect edit_bounds;
  bool edit_visible;
  task_hit *hits;
  size_t hit_count, hit_capacity;
  task_hit pressed;
  float down_x, down_y, last_y, scroll, max_scroll;
  bool scrolling, reordering, in_list;
  uint32_t dragged, editing;
  ui_rect list, hit_clip;
} task_screen;
void task_screen_init(task_screen *, task_model *);
void task_screen_free(task_screen *);
void task_screen_draw(task_screen *, ui_surface *);
void task_screen_down(task_screen *, float, float);
void task_screen_move(task_screen *, float, float);
void task_screen_up(task_screen *, float, float, bool);
#endif
