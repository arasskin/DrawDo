#include "datafriend_ui.h"
bool profile_enabled;
profile_metrics profile_current;
bool ui_error(ui_store *s, const char *message) {
  snprintf(s->error, sizeof(s->error), "%s", message);
  return false;
}
bool ui_open(ui_store *s, const char *path, const char *name, int mode, size_t bytes) {
  memset(s, 0, sizeof(*s));
  df_buffer error = {0};
  df_options options = {path, name, (df_durability)mode, bytes};
  s->engine = df_store_open(&options, &error);
  if (!s->engine)
    snprintf(s->error, sizeof(s->error), "%.*s", (int)minimum(error.size, 511),
             (const char *)error.data);
  df_free(error.data);
  return s->engine != NULL;
}
void ui_close(ui_store *s) {
  df_buffer error = {0};
  if (df_store_close(s->engine, &error) != DF_OK)
    fprintf(stderr, "Datafriend close: %.*s\n", (int)error.size, (const char *)error.data);
  df_free(error.data);
  memset(s, 0, sizeof(*s));
}
df_watch *ui_subscribe(ui_store *s, const df_match *clauses, size_t count, bool all, df_watch_fn cb,
                       void *ctx) {
  int64_t start = profile_start();
  df_watch *w = df_watch_query(s->engine, (df_query_spec){clauses, count, all}, cb, ctx);
  if (!w)
    ui_error(s, df_store_error(s->engine));
  if (profile_enabled && w)
    ++profile_current.subscriptions;
  profile_end(&profile_current.subscribe, start);
  return w;
}
void ui_unwatch(ui_store *s, df_watch **w) {
  if (!*w)
    return;
  int64_t start = profile_start();
  df_watch_remove(s->engine, w);
  if (profile_enabled)
    ++profile_current.unsubscriptions;
  profile_end(&profile_current.unsubscribe, start);
}
bool ui_change(ui_store *s, df_edit *e) {
  int64_t start = profile_start();
  bool ok = df_edit_apply(s->engine, e) == DF_OK;
  if (!ok)
    ui_error(s, df_store_error(s->engine));
  profile_end(&profile_current.engine, start); /* Includes watch dispatch. */
  if (profile_enabled)
    ++profile_current.edits;
  df_edit_free(e);
  return ok;
}
bool ui_delete(ui_store *s, uint32_t id) {
  df_edit e = {0};
  df_edit_delete(&e, id);
  return ui_change(s, &e);
}
bool ui_result_bytes(df_view *r, uint32_t id, uint32_t a, df_buffer *out) {
  df_slice bytes;
  bool ok = df_view_get(r, id, a, &bytes) == DF_OK;
  *out = (df_buffer){(uint8_t *)bytes.data, bytes.size};
  return ok;
}
int64_t ui_integer(df_view *r, uint32_t id, uint32_t a) {
  df_slice b;
  int64_t n = 0;
  if (df_view_get(r, id, a, &b) == DF_OK)
    df_value_int(b, &n);
  return n;
}
bool ui_flag(df_view *r, uint32_t id, uint32_t a) {
  df_slice b;
  bool value = false;
  if (df_view_get(r, id, a, &b) == DF_OK)
    df_value_bool(b, &value);
  return value;
}
bool ui_result_text(df_view *r, uint32_t id, uint32_t a, app_buffer *out) {
  df_slice b, text;
  return df_view_get(r, id, a, &b) == DF_OK && df_value_text(b, &text) == DF_OK &&
         app_string(out, (const char *)text.data, text.size);
}
bool ui_ids(df_view *r, uint32_t order, df_entities *out) {
  return df_view_entities(r, order, 0, out) == DF_OK;
}
