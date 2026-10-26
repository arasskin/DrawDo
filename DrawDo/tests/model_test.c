#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "model.h"
#include <unistd.h>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                   \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)
int main(void) {
  char path[] = "/tmp/drawdo-model-XXXXXX";
  CHECK(mkdtemp(path));
  df_buffer error = {0};
  df_options options = {path, "drawdo-test", DF_ASYNC, 16u << 20};
  df_store *store = df_store_open(&options, &error);
  CHECK(store);
  /* Settings must not assume fixed entity IDs or collide with existing data. */
  df_edit setup = {0};
  CHECK(df_edit_set(&setup, 1, DF_ATTR_NAME, df_string_z("Existing record")));
  CHECK(df_edit_apply(store, &setup) == DF_OK);
  df_edit_free(&setup);
  todo_model m;
  CHECK(todo_open(&m, store, true));
  CHECK(m.ids.count == 0 && m.style.checkbox_size == 26 && m.style.checkbox_color == 0xf44336ff);
  uint32_t a = todo_add(&m, "  First task  "), b = todo_add(&m, "Second task");
  CHECK(a > 1 && b > a && m.ids.count == 2);
  CHECK(todo_mount(&m, 1));
  CHECK(m.row_count == 1 && !strcmp(ct_bytes_string(&todo_find(&m, a)->title), "First task"));
  uint64_t lists = m.list_updates, rows = m.row_updates;
  CHECK(todo_title(&m, b, "Offscreen edit"));
  CHECK(m.list_updates == lists && m.row_updates == rows);
  CHECK(todo_toggle(&m, a));
  CHECK(todo_find(&m, a)->done && m.list_updates == lists && m.row_updates == rows + 1);
  CHECK(todo_title(&m, a, "Renamed"));
  CHECK(m.list_updates == lists && m.row_updates == rows + 2);
  CHECK(todo_set_style(&m, TODO_UI_CHECKBOX_SIZE, df_real(31)));
  CHECK(todo_set_style(&m, TODO_UI_CHECKBOX_COLOR, df_int(0x334455ff)));
  CHECK(m.style.checkbox_size == 31 && m.style.checkbox_color == 0x334455ff);
  CHECK(m.row_updates == rows + 2 && m.list_updates == lists);
  CHECK(df_edit_unset(&m.edit, m.settings[TODO_UI_CHECKBOX_SIZE - 1].entity, DF_ATTR_VALUE));
  CHECK(df_edit_apply(store, &m.edit) == DF_OK && m.style.checkbox_size == 26);
  CHECK(todo_set_style(&m, TODO_UI_CHECKBOX_SIZE, df_real(31)) && m.style.checkbox_size == 31);
  CHECK(todo_set_style(&m, TODO_UI_FONT_SIZE, df_real(-10)) && m.style.font_size == 8);
  CHECK(todo_set_style(&m, TODO_UI_FONT_SIZE, df_string_z("invalid")) && m.style.font_size == 20);
  CHECK(todo_title(&m, a, " \n "));
  CHECK(m.ids.count == 1 && m.ids.data[0] == b && todo_mount(&m, 1));
  CHECK(!todo_find(&m, a) && todo_find(&m, b));
  CHECK(todo_title(&m, a, "Stale editor"));
  CHECK(m.ids.count == 1);
  CHECK(df_store_flush(store) == DF_OK);
  todo_close(&m);
  CHECK(df_store_close(store, &error) == DF_OK);
  store = df_store_open(&options, &error);
  CHECK(store && todo_open(&m, store, true) && todo_mount(&m, 1));
  CHECK(m.ids.count == 1 && !strcmp(ct_bytes_string(&todo_find(&m, b)->title), "Offscreen edit"));
  CHECK(m.style.checkbox_size == 31 && m.style.checkbox_color == 0x334455ff);
  todo_close(&m);
  CHECK(todo_open(&m, store, false));
  CHECK(!todo_add(&m, "Forbidden") && !todo_toggle(&m, b));
  CHECK(!todo_set_style(&m, TODO_UI_CHECKBOX_SIZE, df_real(42)));
  todo_close(&m);
  CHECK(df_store_close(store, &error) == DF_OK);
  df_free(error.data);
  const char *names[] = {"data.mdb", "lock.mdb", "datafriend.owner"};
  for (size_t i = 0; i < COUNT(names); ++i) {
    char file[256];
    snprintf(file, sizeof(file), "%s/%s", path, names[i]);
    unlink(file);
  }
  rmdir(path);
  puts("DrawDo persistence, projections, ownership and style checks passed");
}
