#ifndef APP_DATAFRIEND_UI_H
#define APP_DATAFRIEND_UI_H
#include "base.h"
#include "profiling/metrics.h"
#include "datafriend/db.h"
/* App-only profiling, error presentation and text-buffer conversion. Native
 * values, batches, watches and publication belong to Datafriend. */
typedef struct ui_store {
  df_store *engine;
  char error[512];
} ui_store;
bool ui_open(ui_store *, const char *, const char *, int, size_t);
void ui_close(ui_store *);
df_watch *ui_subscribe(ui_store *, const df_match *, size_t, bool, df_watch_fn, void *);
void ui_unwatch(ui_store *, df_watch **);
/* App convenience consumes the batch storage after synchronous apply. */
bool ui_change(ui_store *, df_edit *);
bool ui_delete(ui_store *, uint32_t);
bool ui_error(ui_store *, const char *);
bool ui_result_bytes(df_view *, uint32_t, uint32_t, df_buffer *);
int64_t ui_integer(df_view *, uint32_t, uint32_t);
bool ui_flag(df_view *, uint32_t, uint32_t);
bool ui_result_text(df_view *, uint32_t, uint32_t, app_buffer *);
bool ui_ids(df_view *, uint32_t, df_entities *);
#endif
