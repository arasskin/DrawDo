#ifndef DRAWDO_MODEL_H
#define DRAWDO_MODEL_H
#include "base.h"
#include "datafriend/db.h"
/* App setting keys, not entity IDs. Records are found by their stable names. */
enum todo_setting_id {
  TODO_UI_BACKGROUND = 1,
  TODO_UI_TEXT_COLOR = 2,
  TODO_UI_DONE_COLOR = 3,
  TODO_UI_CHECKBOX_COLOR = 4,
  TODO_UI_ADD_COLOR = 5,
  TODO_UI_CHECK_COLOR = 6,
  TODO_UI_FONT_SIZE = 7,
  TODO_UI_CHECKBOX_SIZE = 8,
  TODO_UI_CHECKBOX_STROKE = 9,
  TODO_UI_CHECK_STROKE = 10,
  TODO_UI_MARGIN_LEFT = 11,
  TODO_UI_MARGIN_RIGHT = 12,
  TODO_UI_MARGIN_TOP = 13,
  TODO_UI_MARGIN_BOTTOM = 14,
  TODO_UI_ROW_GAP = 15,
  TODO_UI_ROW_HEIGHT = 16,
  TODO_UI_HIT_SIZE = 17,
  TODO_UI_TEXT_WIDTH = 18,
  TODO_UI_TEXT_GAP = 19,
  TODO_UI_TEXT_INSET = 20,
  TODO_UI_ADD_LABEL = 21,
};
typedef struct todo_style {
  /* Persisted dimensions are logical points; pixels exist only at the native edge. */
  uint32_t background, text_color, done_color, checkbox_color, add_color, check_color;
  float font_size, checkbox_size, checkbox_stroke, check_stroke;
  float margin_left, margin_right, margin_top, margin_bottom;
  float row_gap, row_height, hit_size, text_width, text_gap, text_inset;
  ct_bytes add_label;
} todo_style;
enum { TODO_STYLE_FIELDS = 21 };
typedef struct todo_model todo_model;
typedef struct todo_setting {
  todo_model *model;
  df_watch *watch;
  size_t index;
  uint32_t entity;
  df_entities ids;
} todo_setting;
typedef struct todo_row {
  todo_model *model;
  df_watch *watch;
  uint32_t id;
  ct_bytes title;
  bool done, exists;
} todo_row;
struct todo_model {
  df_store *store; /* borrowed; one namespace, one authoritative owner */
  bool writable, dirty;
  df_watch *list;
  df_entities ids;
  df_edit edit;
  todo_style style;
  todo_setting settings[TODO_STYLE_FIELDS];
  todo_row **rows;
  size_t row_count, row_capacity;
  int64_t last_order;
  uint64_t list_updates, row_updates, style_updates;
  char error[256];
};
bool todo_open(todo_model *, df_store *, bool writable);
void todo_close(todo_model *);
bool todo_mount(todo_model *, size_t count);
todo_row *todo_find(todo_model *, uint32_t);
uint32_t todo_add(todo_model *, const char *);
bool todo_title(todo_model *, uint32_t, const char *); /* empty submission deletes */
bool todo_toggle(todo_model *, uint32_t);
bool todo_set_style(todo_model *, uint32_t setting, df_value);
#endif
