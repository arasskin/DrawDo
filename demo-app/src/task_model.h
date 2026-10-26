#ifndef APP_TASK_MODEL_H
#define APP_TASK_MODEL_H
#include "datafriend_ui.h"
enum {
  A_KIND = DF_ATTR_KIND,
  A_TITLE = DF_ATTR_TITLE,
  A_DONE = DF_ATTR_DONE,
  A_ORDER = DF_ATTR_ORDER,
  A_INITIALIZED = DF_ATTR_APP_INITIALIZED,
  A_FILTER = DF_ATTR_TASK_FILTER,
  A_SELECTED = DF_ATTR_TASK_SELECTED
};
typedef struct task_row {
  uint32_t id;
  app_buffer title;
  bool done, exists;
  uint64_t builds;
  uint64_t *build_count;
  ui_store *db;
  df_watch *watch;
} task_row;
typedef struct task_model {
  ui_store db;
  size_t first, visible;
  df_watch *config, *counter_watch, *list;
  void *ordered;
  app_buffer filter, counter;
  df_entities ids;
  uint32_t selected;
  task_row **rows;
  size_t row_count, row_capacity;
  task_row *detail;
  uint64_t list_builds, row_builds, counter_builds, detail_builds;
} task_model;
bool tasks_open(task_model *, const char *);
void tasks_close(task_model *);
bool tasks_mount(task_model *, size_t, size_t);
task_row *tasks_row(task_model *, uint32_t);
bool tasks_filter(task_model *, const char *);
bool tasks_select(task_model *, uint32_t);
uint32_t tasks_add(task_model *, const char *);
bool tasks_rename(task_model *, uint32_t, const char *);
bool tasks_move(task_model *, uint32_t, size_t);
bool tasks_toggle(task_row *);
bool tasks_seed(task_model *, int);
static inline size_t tasks_count(const task_model *m) { return m->ids.count; }
static inline uint32_t *tasks_ids(task_model *m) { return (uint32_t *)m->ids.data; }
#endif
