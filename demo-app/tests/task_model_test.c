#include "task_screen.h"
#include "test.h"
static void shape(ui_rect r, uint32_t c, bool b) {
  (void)r;
  (void)c;
  (void)b;
}
static void text(const char *s, float x, float y, float z, uint32_t c, float w) {
  (void)s;
  (void)x;
  (void)y;
  (void)z;
  (void)c;
  (void)w;
}
static void clip(ui_rect r) { (void)r; }
static bool editor_called, editor_has_point;
static ui_rect opened_bounds;
static ui_point opened_point;
static void edit(const char *text, ui_rect r, const ui_point *point, ui_text_callback cb,
                 void *ctx) {
  (void)text;
  (void)cb;
  (void)ctx;
  editor_called = true;
  editor_has_point = point != NULL;
  opened_bounds = r;
  if (point)
    opened_point = *point;
}
int main(void) {
  char path[128];
  test_path(path, sizeof(path), "tasks-c99");
  task_model m;
  if (!tasks_open(&m, path)) {
    fprintf(stderr, "open: %s\n", m.db.error);
    return 1;
  }
  CHECK(tasks_count(&m) == 12);
  CHECK(tasks_mount(&m, 0, 5));
  uint32_t id = tasks_ids(&m)[0], other = tasks_ids(&m)[1];
  CHECK(tasks_select(&m, id));
  task_row *stable = tasks_row(&m, id);
  uint64_t list = m.list_builds, row = m.row_builds, counter = m.counter_builds,
           detail = m.detail_builds, other_builds = tasks_row(&m, other)->builds;
  CHECK(tasks_rename(&m, id, "Renamed 🌱"));
  CHECK(m.list_builds == list && m.counter_builds == counter);
  CHECK(m.row_builds == row + 1 && m.detail_builds == detail + 1);
  CHECK(tasks_row(&m, other)->builds == other_builds);
  CHECK(tasks_toggle(stable));
  CHECK(m.list_builds == list && m.counter_builds == counter + 1 && m.detail->done);
  CHECK(tasks_move(&m, id, 3));
  CHECK(tasks_ids(&m)[3] == id && tasks_row(&m, id) == stable);
  CHECK(tasks_filter(&m, "Active"));
  CHECK(!tasks_row(&m, id));
  CHECK(tasks_toggle(m.detail));
  CHECK(tasks_filter(&m, "Completed"));
  CHECK(!tasks_count(&m) && !m.row_count);
  CHECK(tasks_filter(&m, "All"));
  for (int i = 0; i < 30; i++) {
    char title[32];
    snprintf(title, sizeof(title), "Extra %d", i);
    CHECK(tasks_add(&m, title));
  }
  CHECK(tasks_mount(&m, 25, 5));
  CHECK(m.row_count == 5 && df_store_watch_count(m.db.engine) == 9);
  CHECK(tasks_filter(&m, "Active"));
  CHECK(tasks_mount(&m, 0, 5));
  df_edit e = {0};
  for (size_t i = 0; i < tasks_count(&m); i++)
    CHECK(df_edit_set(&e, tasks_ids(&m)[i], A_DONE, df_bool(true)));
  CHECK(ui_change(&m.db, &e));
  CHECK(!tasks_count(&m) && !m.row_count);
  CHECK(tasks_filter(&m, "All"));
  uint32_t saved = tasks_add(&m, "Persist me");
  CHECK(saved && tasks_select(&m, saved) && tasks_rename(&m, saved, "Kept after reopen"));
  uint32_t deleted = tasks_ids(&m)[0];
  CHECK(ui_delete(&m.db, deleted) && tasks_rename(&m, deleted, "Must not resurrect"));
  for (size_t i = 0; i < tasks_count(&m); i++)
    CHECK(tasks_ids(&m)[i] != deleted);
  size_t before = tasks_count(&m);
  CHECK(!tasks_add(&m, "  \n ") && tasks_count(&m) == before);
  CHECK(!tasks_filter(&m, "Invalid"));
  m.db.error[0] = 0;
  task_screen view;
  task_screen_init(&view, &m);
  ui_surface c = {400, 760, shape, text, clip, NULL, NULL};
  task_screen_draw(&view, &c);
  task_screen_down(&view, 280, 196);
  task_screen_up(&view, 280, 196, false);
  CHECK(!strcmp(app_cstr(&m.filter), "Completed"));
  CHECK(tasks_filter(&m, "All"));
  task_screen_draw(&view, &c);
  uint64_t rev = df_revision(m.db.engine);
  task_screen_down(&view, 100, 340);
  task_screen_move(&view, 100, 280);
  task_screen_up(&view, 100, 280, false);
  CHECK(df_revision(m.db.engine) == rev);
  view.edit_text = edit;
  task_screen_draw(&view, &c);
  float scroll = view.scroll;
  uint32_t tapped = tasks_ids(&m)[2];
  task_screen_down(&view, 113, 312);
  task_screen_up(&view, 113, 312, false);
  CHECK(editor_called && editor_has_point && view.editing == tapped);
  CHECK(opened_point.x == 113 && opened_point.y == 312);
  CHECK(view.scroll == scroll && opened_bounds.y == 230 + 2 * 58 - scroll + 16);
  editor_called = false;
  task_screen_draw(&view, &c);
  task_screen_down(&view, 180, 690);
  task_screen_up(&view, 180, 690, false);
  CHECK(editor_called && !editor_has_point); /* Explicit Edit still starts at the end. */
  CHECK(tasks_select(&m, saved));
  task_screen_free(&view);
  tasks_close(&m);
  CHECK(tasks_open(&m, path));
  CHECK(m.selected == saved && m.detail &&
        !strcmp(app_cstr(&m.detail->title), "Kept after reopen"));
  tasks_close(&m);
  CHECK(app_remove_tree(path));
  puts("PASS: narrow publication, scoped rows, ordering, filters, hit testing, stale edits, "
       "persistence");
  return 0;
}
