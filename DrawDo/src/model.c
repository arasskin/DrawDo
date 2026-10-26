#include "model.h"
#include <stddef.h>
typedef struct style_field {
  uint32_t key;
  const char *name;
  size_t offset;
  double initial, low, high;
  bool color;
} style_field;
#define COLOR(a, f, v) {TODO_UI_##a, "ui/" #f, offsetof(todo_style, f), v, 0, UINT32_MAX, true}
#define SIZE(a, f, v, lo, hi) {TODO_UI_##a, "ui/" #f, offsetof(todo_style, f), v, lo, hi, false}
/* Defaults come from the CLJD own-list screen. Seed missing fields only. */
static const style_field fields[] = {COLOR(BACKGROUND, background, 0xffffffff),
                                     COLOR(TEXT_COLOR, text_color, 0x000000ff),
                                     COLOR(DONE_COLOR, done_color, 0x808080ff),
                                     COLOR(CHECKBOX_COLOR, checkbox_color, 0xf44336ff),
                                     COLOR(ADD_COLOR, add_color, 0x9e9e9eff),
                                     COLOR(CHECK_COLOR, check_color, 0xffffffff),
                                     SIZE(FONT_SIZE, font_size, 20, 8, 72),
                                     SIZE(CHECKBOX_SIZE, checkbox_size, 26, 8, 96),
                                     SIZE(CHECKBOX_STROKE, checkbox_stroke, 2, .5, 12),
                                     SIZE(CHECK_STROKE, check_stroke, 2.5, .5, 12),
                                     SIZE(MARGIN_LEFT, margin_left, 40, 0, 256),
                                     SIZE(MARGIN_RIGHT, margin_right, 16, 0, 256),
                                     SIZE(MARGIN_TOP, margin_top, 40, 0, 256),
                                     SIZE(MARGIN_BOTTOM, margin_bottom, 16, 0, 256),
                                     SIZE(ROW_GAP, row_gap, 30, 0, 128),
                                     SIZE(ROW_HEIGHT, row_height, 48, 24, 256),
                                     SIZE(HIT_SIZE, hit_size, 48, 24, 128),
                                     SIZE(TEXT_WIDTH, text_width, 200, 32, 1024),
                                     SIZE(TEXT_GAP, text_gap, 0, 0, 128),
                                     SIZE(TEXT_INSET, text_inset, 12, 0, 64)};
static bool fail(todo_model *m, const char *s) {
  snprintf(m->error, sizeof(m->error), "%s", s);
  return false;
}
static bool apply(todo_model *m) {
  if (!m->writable) {
    df_edit_clear(&m->edit);
    return false;
  }
  if (df_edit_apply(m->store, &m->edit) != DF_OK)
    return fail(m, df_store_error(m->store));
  return !m->error[0];
}
static const char *setting_name(size_t index) {
  return index < COUNT(fields) ? fields[index].name : "ui/add_label";
}
static bool seed(todo_model *m) {
  for (size_t i = 0; i < TODO_STYLE_FIELDS; ++i) {
    todo_setting *slot = m->settings + i;
    df_match clause = {df_var(0), DF_ATTR_NAME, df_const(df_string_z(setting_name(i))), true};
    df_view *view = df_query_once(m->store, (df_query_spec){&clause, 1, false});
    if (!view)
      return fail(m, df_store_error(m->store));
    bool ok = df_view_entities(view, 0, 0, &slot->ids) == DF_OK && slot->ids.count <= 1;
    df_view_release(view);
    if (!ok)
      return fail(m, "Duplicate or invalid UI setting");
    uint32_t id = slot->ids.count ? slot->ids.data[0] : df_edit_create(&m->edit, m->store);
    if (!id)
      return fail(m, "Cannot allocate UI setting");
    if (!slot->ids.count)
      df_edit_set(&m->edit, id, DF_ATTR_NAME, df_string_z(setting_name(i)));
    df_slice v;
    df_status status = df_store_get(m->store, id, DF_ATTR_VALUE, &v);
    if (status == DF_ERROR)
      return fail(m, df_store_error(m->store));
    if (status == DF_ABSENT) {
      df_value value = df_string_z("");
      if (i < COUNT(fields))
        value = fields[i].color ? df_int((int64_t)fields[i].initial) : df_real(fields[i].initial);
      df_edit_set(&m->edit, id, DF_ATTR_VALUE, value);
    }
  }
  return apply(m);
}
static void style_changed(void *context, df_view *view) {
  todo_setting *slot = context;
  todo_model *m = slot->model;
  df_slice value;
  ++m->style_updates;
  m->dirty = true;
  if (df_view_entities(view, 0, 0, &slot->ids) != DF_OK || slot->ids.count > 1) {
    fail(m, "Duplicate or invalid UI setting");
    return;
  }
  slot->entity = slot->ids.count ? slot->ids.data[0] : 0;
  if (slot->index == COUNT(fields)) {
    df_slice text = {0};
    if (df_view_get(view, slot->entity, DF_ATTR_VALUE, &value) == DF_OK)
      df_value_text(value, &text);
    if (!ct_bytes_set(&m->style.add_label, (const char *)text.data, text.size))
      fail(m, "Cannot cache add label");
    return;
  }
  const style_field *f = fields + slot->index;
  double number = f->initial;
  if (df_view_get(view, slot->entity, DF_ATTR_VALUE, &value) == DF_OK) {
    int64_t integer;
    if (df_value_real(value, &number) != DF_OK) {
      if (df_value_int(value, &integer) == DF_OK)
        number = (double)integer;
      else
        number = f->initial;
    }
  }
  if (!isfinite(number))
    number = f->initial;
  number = clamp(number, f->low, f->high);
  void *destination = (char *)&m->style + f->offset;
  if (f->color)
    *(uint32_t *)destination = (uint32_t)number;
  else
    *(float *)destination = (float)number;
}
static void list_changed(void *context, df_view *view) {
  todo_model *m = context;
  ++m->list_updates;
  m->dirty = true;
  if (df_view_entities(view, DF_ATTR_ORDER, 0, &m->ids) != DF_OK) {
    fail(m, "Cannot cache task order");
    return;
  }
  m->last_order = -1;
  if (m->ids.count) {
    df_slice value;
    if (df_view_get(view, m->ids.data[m->ids.count - 1], DF_ATTR_ORDER, &value) != DF_OK ||
        df_value_int(value, &m->last_order) != DF_OK)
      fail(m, "Invalid task order");
  }
}
static void row_changed(void *context, df_view *view) {
  todo_row *r = context;
  todo_model *m = r->model;
  ++m->row_updates;
  m->dirty = true;
  df_slice value, text;
  r->exists = df_view_get(view, r->id, DF_ATTR_TITLE, &value) == DF_OK;
  if (!r->exists)
    return;
  if (df_value_text(value, &text) != DF_OK ||
      !ct_bytes_set(&r->title, (const char *)text.data, text.size) ||
      df_view_get(view, r->id, DF_ATTR_DONE, &value) != DF_OK ||
      df_value_bool(value, &r->done) != DF_OK)
    fail(m, "Invalid task row");
}
todo_row *todo_find(todo_model *m, uint32_t id) {
  for (size_t i = 0; i < m->row_count; ++i)
    if (m->rows[i]->id == id)
      return m->rows[i];
  return NULL;
}
static void row_close(todo_row *r) {
  df_watch_remove(r->model->store, &r->watch);
  ct_bytes_free(&r->title);
  free(r);
}
bool todo_mount(todo_model *m, size_t count) {
  if (count > m->ids.count)
    count = m->ids.count;
  for (size_t i = 0; i < m->row_count;) {
    bool keep = false;
    for (size_t j = 0; j < count; ++j)
      if (m->rows[i]->id == m->ids.data[j])
        keep = true;
    if (keep) {
      ++i;
      continue;
    }
    row_close(m->rows[i]);
    m->rows[i] = m->rows[--m->row_count];
  }
  for (size_t i = 0; i < count; ++i) {
    if (todo_find(m, m->ids.data[i]))
      continue;
    if (!ct_array_reserve(&m->rows, &m->row_capacity, m->row_count + 1, sizeof(*m->rows)))
      return fail(m, "Cannot mount task rows");
    todo_row *r = calloc(1, sizeof(*r));
    if (!r)
      return fail(m, "Cannot allocate task row");
    r->model = m;
    r->id = m->ids.data[i];
    uint32_t attributes[] = {DF_ATTR_TITLE, DF_ATTR_DONE};
    r->watch = df_watch_entity(m->store, r->id, attributes, COUNT(attributes), row_changed, r);
    if (!r->watch) {
      row_close(r);
      return fail(m, df_store_error(m->store));
    }
    m->rows[m->row_count++] = r;
  }
  return !m->error[0];
}
bool todo_open(todo_model *m, df_store *store, bool writable) {
  memset(m, 0, sizeof(*m));
  m->store = store;
  m->writable = writable;
  if (writable && !seed(m))
    return false;
  for (size_t i = 0; i < TODO_STYLE_FIELDS; ++i) {
    todo_setting *s = m->settings + i;
    s->model = m;
    s->index = i;
    df_match clauses[] = {{df_var(0), DF_ATTR_NAME, df_const(df_string_z(setting_name(i))), true},
                          {df_var(0), DF_ATTR_VALUE, df_var(1), true}};
    s->watch =
        df_watch_query(store, (df_query_spec){clauses, COUNT(clauses), false}, style_changed, s);
    if (!s->watch)
      return fail(m, df_store_error(store));
  }
  df_match q[] = {{df_var(0), DF_ATTR_KIND, df_const(df_string_z("task")), false},
                  {df_var(0), DF_ATTR_ORDER, df_var(1), true}};
  m->list = df_watch_query(store, (df_query_spec){q, COUNT(q), false}, list_changed, m);
  return m->list && !m->error[0];
}
void todo_close(todo_model *m) {
  for (size_t i = 0; i < m->row_count; ++i)
    row_close(m->rows[i]);
  free(m->rows);
  df_watch_remove(m->store, &m->list);
  for (size_t i = 0; i < TODO_STYLE_FIELDS; ++i) {
    df_watch_remove(m->store, &m->settings[i].watch);
    df_entities_free(&m->settings[i].ids);
  }
  df_entities_free(&m->ids);
  df_edit_free(&m->edit);
  ct_bytes_free(&m->style.add_label);
  memset(m, 0, sizeof(*m));
}
static size_t trimmed(const char **text) {
  while (**text && strchr(" \t\r\n", **text))
    ++*text;
  size_t n = strlen(*text);
  while (n && strchr(" \t\r\n", (*text)[n - 1]))
    --n;
  return n;
}
uint32_t todo_add(todo_model *m, const char *text) {
  size_t n = trimmed(&text);
  if (!m->writable || !n)
    return 0;
  if (m->last_order == INT64_MAX) {
    fail(m, "Task order exhausted");
    return 0;
  }
  uint32_t id = df_edit_create(&m->edit, m->store);
  if (!id) {
    df_edit_clear(&m->edit);
    fail(m, "Cannot allocate task ID");
    return 0;
  }
  df_edit_set(&m->edit, id, DF_ATTR_KIND, df_string_z("task"));
  df_edit_set(&m->edit, id, DF_ATTR_TITLE, df_string(text, n));
  df_edit_set(&m->edit, id, DF_ATTR_DONE, df_bool(false));
  df_edit_set(&m->edit, id, DF_ATTR_ORDER, df_int(m->last_order + 1));
  return apply(m) ? id : 0;
}
bool todo_title(todo_model *m, uint32_t id, const char *text) {
  df_slice value;
  if (!m->writable)
    return false;
  if (df_store_get(m->store, id, DF_ATTR_DONE, &value) == DF_ABSENT)
    return true;
  size_t n = trimmed(&text);
  if (n)
    df_edit_set(&m->edit, id, DF_ATTR_TITLE, df_string(text, n));
  else
    df_edit_delete(&m->edit, id);
  return apply(m);
}
bool todo_toggle(todo_model *m, uint32_t id) {
  df_slice value;
  bool done;
  if (!m->writable)
    return false;
  if (df_store_get(m->store, id, DF_ATTR_DONE, &value) != DF_OK ||
      df_value_bool(value, &done) != DF_OK)
    return false;
  df_edit_set(&m->edit, id, DF_ATTR_DONE, df_bool(!done));
  return apply(m);
}
bool todo_set_style(todo_model *m, uint32_t setting, df_value value) {
  if (!m->writable || !setting || setting > TODO_STYLE_FIELDS)
    return false;
  todo_setting *slot = m->settings + setting - 1;
  if (!slot->entity && !seed(m))
    return false;
  if (!slot->entity)
    return fail(m, "Missing UI setting");
  df_edit_set(&m->edit, slot->entity, DF_ATTR_VALUE, value);
  return apply(m);
}
