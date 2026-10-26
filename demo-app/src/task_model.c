#include "task_model.h"
static const char *filters[] = {"All", "Active", "Completed"};
static bool edit_one(ui_store *s, uint32_t id, uint32_t a, df_value v) {
  df_edit e = {0};
  df_edit_set(&e, id, a, v);
  return ui_change(s, &e);
}
static bool trim(app_buffer *b, const char *s) {
  while (*s && strchr(" \t\r\n", *s))
    s++;
  size_t n = strlen(s);
  while (n && strchr(" \t\r\n", s[n - 1]))
    n--;
  return app_string(b, s, n);
}
static void row_build(void *ctx, df_view *result) {
  task_row *r = ctx;
  r->builds++;
  (*r->build_count)++;
  r->exists = df_view_size(result) > 0;
  if (r->exists) {
    if (!ui_result_text(result, r->id, A_TITLE, &r->title))
      ui_error(r->db, "Invalid task title");
    r->done = ui_flag(result, r->id, A_DONE);
  }
}
static task_row *row_open(task_model *m, uint32_t id, uint64_t *count) {
  task_row *r = calloc(1, sizeof(*r));
  if (!r) {
    ui_error(&m->db, "Row allocation failed");
    return NULL;
  }
  r->id = id;
  r->db = &m->db;
  r->build_count = count;
  df_match q[] = {{df_id(id), A_TITLE, df_var(0), true}, {df_id(id), A_DONE, df_var(1), true}};
  r->watch = ui_subscribe(&m->db, q, COUNT(q), false, row_build, r);
  if (!r->watch) {
    app_buffer_free(&r->title);
    free(r);
    return NULL;
  }
  return r;
}
static void row_close(task_row *r) {
  if (!r)
    return;
  ui_unwatch(r->db, &r->watch);
  app_buffer_free(&r->title);
  free(r);
}
task_row *tasks_row(task_model *m, uint32_t id) {
  for (size_t i = 0; i < m->row_count; i++)
    if (m->rows[i]->id == id)
      return m->rows[i];
  return NULL;
}
bool tasks_mount(task_model *m, size_t first, size_t count) {
  m->first = (size_t)minimum(first, tasks_count(m));
  m->visible = count;
  size_t end = m->first + (size_t)minimum(count, tasks_count(m) - m->first);
  uint32_t *ids = tasks_ids(m);
  for (size_t i = 0; i < m->row_count;) {
    bool keep = false;
    for (size_t j = m->first; j < end; j++)
      if (ids[j] == m->rows[i]->id)
        keep = true;
    if (keep) {
      i++;
      continue;
    }
    row_close(m->rows[i]);
    memmove(m->rows + i, m->rows + i + 1, (--m->row_count - i) * sizeof(*m->rows));
  }
  for (size_t i = m->first; i < end; i++)
    if (!tasks_row(m, ids[i])) {
      if (!app_reserve(&m->rows, &m->row_capacity, m->row_count + 1, sizeof(*m->rows)))
        return ui_error(&m->db, "Row list allocation failed");
      task_row *r = row_open(m, ids[i], &m->row_builds);
      if (!r)
        return false;
      m->rows[m->row_count++] = r;
    }
  return true;
}
static void list_build(void *ctx, df_view *r) {
  task_model *m = ctx;
  m->list_builds++;
  df_result_release(m->ordered);
  df_view_keep(r);
  m->ordered = r;
  if (!ui_ids(r, A_ORDER, &m->ids) || !tasks_mount(m, m->first, m->visible))
    ui_error(&m->db, "Task list publication failed");
}
static bool query_list(task_model *m) {
  ui_unwatch(&m->db, &m->list);
  df_match q[] = {
      {df_var(0), A_KIND, df_const(df_string_z("task")), false},
      {df_var(0), A_ORDER, df_var(1), true},
      {df_var(0), A_DONE, df_const(df_bool(!strcmp(app_cstr(&m->filter), "Completed"))), false}};
  m->list =
      ui_subscribe(&m->db, q, !strcmp(app_cstr(&m->filter), "All") ? 2 : 3, false, list_build, m);
  return m->list != NULL;
}
static void counter_build(void *ctx, df_view *r) {
  task_model *m = ctx;
  df_entities ids = {0};
  uint8_t yes[] = {1, 1};
  m->counter_builds++;
  if (!ui_ids(r, 0, &ids) ||
      !app_format(&m->counter, "%lld of %zu complete",
                  (long long)df_result_count_value(r, A_DONE, yes, 2), ids.count))
    ui_error(&m->db, "Counter publication failed");
  df_entities_free(&ids);
}
static void config_build(void *ctx, df_view *r) {
  task_model *m = ctx;
  app_buffer filter = {0};
  if (!ui_result_text(r, 1, A_FILTER, &filter)) {
    ui_error(&m->db, "Missing task filter");
    return;
  }
  if (strcmp(app_cstr(&m->filter), app_cstr(&filter))) {
    app_buffer_free(&m->filter);
    m->filter = filter;
    filter = (app_buffer){0};
    m->first = 0;
    query_list(m);
  }
  app_buffer_free(&filter);
  uint32_t id = (uint32_t)ui_integer(r, 1, A_SELECTED);
  if (id != m->selected) {
    m->selected = id;
    row_close(m->detail);
    m->detail = id ? row_open(m, id, &m->detail_builds) : NULL;
  }
}
bool tasks_open(task_model *m, const char *directory) {
  memset(m, 0, sizeof(*m));
  if (!ui_open(&m->db, directory, "default", 2, (size_t)1 << 30))
    return false;
  df_match marker = {df_id(1), A_INITIALIZED, df_const(df_bool(true)), true};
  df_watch *w = ui_subscribe(&m->db, &marker, 1, false, NULL, NULL);
  if (!w)
    return false;
  bool initialized = df_view_size(w->result) > 0;
  ui_unwatch(&m->db, &w);
  if (!initialized) {
    df_edit e = {0};
    df_edit_set(&e, 1, A_INITIALIZED, df_bool(true));
    df_edit_set(&e, 1, A_FILTER, df_string_z("All"));
    df_edit_set(&e, 1, A_SELECTED, df_entity(0));
    if (!ui_change(&m->db, &e))
      return false;
    const char *titles[] = {
        "Make something small",    "Take a walk outside", "Read a chapter",
        "Call a friend",           "Sketch an idea",      "Water the plants",
        "Try a new recipe",        "Tidy the desk",       "Listen to an album",
        "Plan a little adventure", "Write a note",        "Leave room for play"};
    for (size_t i = 0; i < COUNT(titles); i++) {
      uint32_t id = df_edit_create(&e, m->db.engine);
      if (!id) {
        df_edit_free(&e);
        return false;
      }
      df_edit_set(&e, id, A_KIND, df_string_z("task"));
      df_edit_set(&e, id, A_TITLE, df_string_z(titles[i]));
      df_edit_set(&e, id, A_DONE, df_bool(false));
      df_edit_set(&e, id, A_ORDER, df_int((int64_t)i));
    }
    if (!ui_change(&m->db, &e))
      return false;
  }
  df_match counter[] = {{df_var(0), A_KIND, df_const(df_string_z("task")), false},
                        {df_var(0), A_DONE, df_var(1), true}};
  m->counter_watch = ui_subscribe(&m->db, counter, 2, false, counter_build, m);
  df_match config[] = {{df_id(1), A_FILTER, df_var(0), true},
                       {df_id(1), A_SELECTED, df_var(1), true}};
  m->config = ui_subscribe(&m->db, config, 2, false, config_build, m);
  return m->counter_watch && m->config && m->list && !m->db.error[0];
}
void tasks_close(task_model *m) {
  row_close(m->detail);
  for (size_t i = 0; i < m->row_count; i++)
    row_close(m->rows[i]);
  free(m->rows);
  df_result_release(m->ordered);
  ui_close(&m->db);
  app_buffer_free(&m->filter);
  app_buffer_free(&m->counter);
  df_entities_free(&m->ids);
  memset(m, 0, sizeof(*m));
}
bool tasks_filter(task_model *m, const char *filter) {
  bool valid = false;
  for (size_t i = 0; i < COUNT(filters); i++)
    if (!strcmp(filter, filters[i]))
      valid = true;
  if (!valid)
    return ui_error(&m->db, "Unknown filter");
  return edit_one(&m->db, 1, A_FILTER, df_string_z(filter));
}
bool tasks_select(task_model *m, uint32_t id) {
  return edit_one(&m->db, 1, A_SELECTED, df_entity(id));
}
bool tasks_toggle(task_row *r) {
  return !r->exists || edit_one(r->db, r->id, A_DONE, df_bool(!r->done));
}
uint32_t tasks_add(task_model *m, const char *title) {
  app_buffer t = {0};
  if (!trim(&t, title)) {
    ui_error(&m->db, "Title allocation failed");
    return 0;
  }
  if (!t.size) {
    app_buffer_free(&t);
    return 0;
  }
  df_match q[] = {{df_var(0), A_KIND, df_const(df_string_z("task")), false},
                  {df_var(0), A_ORDER, df_var(1), true}};
  df_view *result = df_query_once(m->db.engine, (df_query_spec){q, 2, false});
  df_entities ids = {0};
  int64_t last = -1;
  if (!result || !ui_ids(result, 0, &ids)) {
    df_view_release(result);
    df_entities_free(&ids);
    app_buffer_free(&t);
    return 0;
  }
  for (size_t i = 0; i < ids.count; i++) {
    int64_t order = ui_integer(result, ids.data[i], A_ORDER);
    if (order > last)
      last = order;
  }
  df_view_release(result);
  df_entities_free(&ids);
  df_edit e = {0};
  uint32_t id = df_edit_create(&e, m->db.engine);
  df_edit_set(&e, id, A_KIND, df_string_z("task"));
  df_edit_set(&e, id, A_TITLE, df_string_z(app_cstr(&t)));
  df_edit_set(&e, id, A_DONE, df_bool(false));
  df_edit_set(&e, id, A_ORDER, df_int(last + 1));
  bool ok = id && ui_change(&m->db, &e);
  df_edit_free(&e);
  app_buffer_free(&t);
  return ok ? id : 0;
}
bool tasks_rename(task_model *m, uint32_t id, const char *title) {
  app_buffer t = {0};
  df_slice title_value, done_value;
  bool exists = df_store_get(m->db.engine, id, A_TITLE, &title_value) == DF_OK &&
                df_store_get(m->db.engine, id, A_DONE, &done_value) == DF_OK;
  bool ok = trim(&t, title);
  if (ok && exists && t.size)
    ok = edit_one(&m->db, id, A_TITLE, df_string_z(app_cstr(&t)));
  app_buffer_free(&t);
  return ok;
}
bool tasks_move(task_model *m, uint32_t id, size_t destination) {
  size_t n = tasks_count(m), from = 0;
  uint32_t *ids = tasks_ids(m);
  while (from < n && ids[from] != id)
    from++;
  if (from == n || !n)
    return true;
  if (destination >= n)
    destination = n - 1;
  if (from == destination)
    return true;
  df_edit e = {0};
  size_t lo = from < destination ? from : destination, hi = from > destination ? from : destination;
  for (size_t i = lo; i <= hi; i++) {
    uint32_t target = i == destination ? id : ids[from < destination ? i + 1 : i - 1];
    df_edit_set(&e, target, A_ORDER, df_int(ui_integer(m->ordered, ids[i], A_ORDER)));
  }
  return ui_change(&m->db, &e);
}
bool tasks_seed(task_model *m, int count) {
  df_edit e = {0};
  for (int i = (int)tasks_count(m); i < count; i++) {
    uint32_t id = df_edit_create(&e, m->db.engine);
    char title[64];
    snprintf(title, sizeof(title), "Profile task %d", i);
    df_edit_set(&e, id, A_KIND, df_string_z("task"));
    df_edit_set(&e, id, A_TITLE, df_string_z(title));
    df_edit_set(&e, id, A_DONE, df_bool(false));
    df_edit_set(&e, id, A_ORDER, df_int(i));
  }
  if (e.count && !ui_change(&m->db, &e))
    return false;
  for (size_t i = 0; i < tasks_count(m); i++)
    df_edit_set(&e, tasks_ids(m)[i], A_DONE, df_bool(i % 2));
  return ui_change(&m->db, &e) && tasks_select(m, tasks_ids(m)[0]);
}
