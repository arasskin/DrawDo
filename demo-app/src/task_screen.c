#include "task_screen.h"
static const uint32_t ink = 0xe8f2f7ff, muted = 0x87a5b6ff, mint = 0x70e7baff;
enum { H_ADD = 1, H_FILTER, H_TOGGLE, H_EDIT, H_DELETE, H_UP, H_DOWN, H_TITLE };
void task_screen_init(task_screen *v, task_model *m) {
  memset(v, 0, sizeof(*v));
  v->model = m;
}
void task_screen_free(task_screen *v) {
  free(v->hits);
  memset(v, 0, sizeof(*v));
}
static void hit(task_screen *v, ui_rect r, int action, uint32_t id) {
  float right = (float)minimum(r.x + r.w, v->hit_clip.x + v->hit_clip.w),
        bottom = (float)minimum(r.y + r.h, v->hit_clip.y + v->hit_clip.h);
  r.x = (float)maximum(r.x, v->hit_clip.x);
  r.y = (float)maximum(r.y, v->hit_clip.y);
  r.w = right - r.x;
  r.h = bottom - r.y;
  if (r.w <= 0 || r.h <= 0)
    return;
  if (!app_reserve(&v->hits, &v->hit_capacity, v->hit_count + 1, sizeof(*v->hits))) {
    ui_error(&v->model->db, "Hit target allocation failed");
    return;
  }
  v->hits[v->hit_count++] = (task_hit){r, action, id};
}
static void accessible(task_screen *v, ui_surface *c, ui_rect r, int action, uint32_t id,
                       const char *label, const char *value, unsigned flags) {
  bool in_list = ui_contains(v->list, r.x + r.w / 2, r.y + r.h / 2);
  float right = (float)minimum(r.x + r.w, v->hit_clip.x + v->hit_clip.w);
  float bottom = (float)minimum(r.y + r.h, v->hit_clip.y + v->hit_clip.h);
  r.x = (float)maximum(r.x, v->hit_clip.x);
  r.y = (float)maximum(r.y, v->hit_clip.y);
  r.w = right - r.x;
  r.h = bottom - r.y;
  if (r.w <= 0 || r.h <= 0)
    return;
  uint64_t key = ((uint64_t)id << 8) | (unsigned)(100 + action);
  if (!in_list)
    key |= UINT64_C(1) << 48;
  ui_access(c, key, r, label, value, flags);
}
static void button(task_screen *v, ui_surface *c, ui_rect r, const char *label, bool selected,
                   int action, uint32_t id) {
  c->shape(r, selected ? 0x28584fff : 0x20323fff, false);
  c->text(label, r.x + 10, r.y + 9, 13, selected ? mint : ink, r.w - 20);
  hit(v, r, action, id);
  task_row *row = tasks_row(v->model, id);
  if (!row && v->model->detail && v->model->detail->id == id)
    row = v->model->detail;
  const char *value = row ? app_cstr(&row->title) : "";
  unsigned flags = ACCESS_BUTTON | (selected ? ACCESS_SELECTED : 0);
  if (action == H_TOGGLE && row) {
    label = value;
    value = "";
    flags = ACCESS_CHECKBOX | (row->done ? ACCESS_CHECKED : 0);
  } else if (action == H_DELETE)
    label = "Delete task";
  else if (action == H_UP)
    label = "Move task up";
  else if (action == H_DOWN)
    label = "Move task down";
  if (action == H_UP || action == H_DOWN) {
    size_t index = 0;
    while (index < tasks_count(v->model) && tasks_ids(v->model)[index] != id)
      index++;
    if (index == tasks_count(v->model) || (action == H_UP && !index) ||
        (action == H_DOWN && index + 1 == tasks_count(v->model)))
      flags |= ACCESS_DISABLED;
  }
  accessible(v, c, r, action, id, label, value, flags);
}
static void submitted(void *ctx, const char *text) {
  task_screen *v = ctx;
  if (v->editing)
    tasks_rename(v->model, v->editing, text);
  else if (tasks_add(v->model, text))
    v->scroll = 1e9f;
}
static void act(task_screen *v, task_hit h, ui_point point) {
  task_model *m = v->model;
  m->db.error[0] = 0;
  size_t index = 0;
  while (index < tasks_count(m) && tasks_ids(m)[index] != h.id)
    index++;
  switch (h.action) {
  case H_ADD:
    v->editing = 0;
    if (v->edit_text)
      v->edit_text("", (ui_rect){28, 125, 344, 28}, NULL, submitted, v);
    break;
  case H_FILTER: {
    const char *filters[] = {"All", "Active", "Completed"};
    tasks_filter(m, filters[h.id]);
    v->scroll = 0;
    break;
  }
  case H_TOGGLE: {
    task_row *r = tasks_row(m, h.id);
    if (!r && m->detail && m->detail->id == h.id)
      r = m->detail;
    if (r)
      tasks_toggle(r);
    break;
  }
  case H_DELETE:
    ui_delete(&m->db, h.id);
    break;
  case H_UP:
    if (index && index < tasks_count(m))
      tasks_move(m, h.id, index - 1);
    break;
  case H_DOWN:
    if (index < tasks_count(m))
      tasks_move(m, h.id, index + 1);
    break;
  case H_TITLE:
  case H_EDIT:
    tasks_select(m, h.id);
    if (!m->detail || !m->detail->exists)
      return;
    if (h.action == H_EDIT && index < tasks_count(m))
      v->scroll = (float)clamp(index * 58, 0, v->max_scroll);
    float y = v->list.y;
    if (index < tasks_count(m))
      y += (float)index * 58 - v->scroll;
    v->editing = h.id;
    if (v->edit_text)
      v->edit_text(app_cstr(&m->detail->title), (ui_rect){72, y + 16, 234, 26},
                   h.action == H_TITLE ? &point : NULL, submitted, v);
    break;
  default:
    break;
  }
}
void task_screen_draw(task_screen *v, ui_surface *c) {
  task_model *m = v->model;
  v->hit_count = 0;
  v->edit_visible = false;
  bool editing = v->session && v->session->active && v->session->context == v;
  v->hit_clip = (ui_rect){0, 0, c->width, c->height};
  c->clip(v->hit_clip);
  c->text("A little more done.", 18, 24, 27, ink, 278);
  c->text("Datafriend · stored on this device", 19, 63, 12, muted, 350);
  c->text(app_cstr(&m->counter), 19, 87, 12, mint, 350);
  button(v, c, (ui_rect){18, 116, 364, 46}, "+   What would you like to get done?", false, H_ADD,
         0);
  if (editing && !v->editing) {
    v->edit_visible = true;
    v->edit_bounds = (ui_rect){28, 125, 344, 28};
  }
  const char *filters[] = {"All", "Active", "Completed"};
  for (int i = 0; i < 3; i++)
    button(v, c, (ui_rect){18.0f + i * 124, 179, 116, 36}, filters[i],
           !strcmp(app_cstr(&m->filter), filters[i]), H_FILTER, (uint32_t)i);
  bool detail = m->detail && m->detail->exists;
  v->list = (ui_rect){18, 230, 364, (float)maximum(58, c->height - 230 - (detail ? 150 : 48))};
  v->max_scroll = (float)maximum(0, tasks_count(m) * 58 - v->list.h);
  v->scroll = (float)clamp(v->scroll, 0, v->max_scroll);
  if (v->reordering) {
    if (v->last_y < v->list.y + 25)
      v->scroll = (float)maximum(0, v->scroll - 4);
    if (v->last_y > v->list.y + v->list.h - 25)
      v->scroll = (float)minimum(v->max_scroll, v->scroll + 4);
  }
  size_t first = (size_t)(v->scroll / 58), visible = (size_t)ceil(v->list.h / 58) + 1;
  if (!tasks_mount(m, first, visible))
    return;
  ui_access(c, 250, v->list, "Task list", "", ACCESS_SCROLL);
  c->shape(v->list, 0x14232eff, false);
  c->clip(v->list);
  v->hit_clip = v->list;
  for (size_t i = first; i < tasks_count(m) && i < first + visible; i++) {
    uint32_t id = tasks_ids(m)[i];
    task_row *r = tasks_row(m, id);
    if (!r)
      continue;
    float y = v->list.y + (float)i * 58 - v->scroll;
    c->shape((ui_rect){18, y, 364, 56}, m->selected == id ? 0x1f363eff : 0x172a36ff, false);
    button(v, c, (ui_rect){26, y + 10, 36, 36}, r->done ? "✓" : "", r->done, H_TOGGLE, id);
    if (editing && v->editing == id) {
      v->edit_bounds = (ui_rect){72, y + 16, 234, 26};
      v->edit_visible = y + 16 >= v->list.y && y + 42 <= v->list.y + v->list.h;
    }
    c->text(app_cstr(&r->title), 72, y + 16, 16, r->done ? muted : ink, 234);
    if (r->done)
      c->shape((ui_rect){72, y + 31, 224, 1}, 0x87a5b699, false);
    hit(v, (ui_rect){68, y, 246, 58}, H_TITLE, id);
    accessible(v, c, (ui_rect){68, y, 246, 58}, H_TITLE, id, app_cstr(&r->title), "Edit title",
               ACCESS_BUTTON);
    button(v, c, (ui_rect){315, y + 10, 30, 36}, "×", false, H_DELETE, id);
    c->text("≡", 357, y + 13, 20, muted, 22);
  }
  if (!tasks_count(m))
    c->text("A clear list. Room for something good.", 33, v->list.y + 30, 14, muted, 332);
  if (v->reordering) {
    float y = v->list.y + floorf((v->last_y - v->list.y + v->scroll) / 58) * 58 - v->scroll;
    c->shape((ui_rect){18, y, 364, 3}, mint, false);
  }
  if (v->max_scroll > 0) {
    float thumb = (float)maximum(24, v->list.h * v->list.h / (tasks_count(m) * 58));
    c->shape((ui_rect){379, v->list.y + (v->list.h - thumb) * v->scroll / v->max_scroll, 3, thumb},
             0x70e7baaa, false);
  }
  v->hit_clip = (ui_rect){0, 0, c->width, c->height};
  c->clip(v->hit_clip);
  if (detail) {
    float y = c->height - 134;
    uint32_t id = m->detail->id;
    c->shape((ui_rect){18, y, 364, 96}, 0x20323fff, false);
    c->text(app_cstr(&m->detail->title), 30, y + 10, 16, ink, 340);
    button(v, c, (ui_rect){26, y + 46, 114, 38}, m->detail->done ? "Mark active" : "Mark done",
           false, H_TOGGLE, id);
    button(v, c, (ui_rect){148, y + 46, 65, 38}, "Edit", false, H_EDIT, id);
    button(v, c, (ui_rect){221, y + 46, 75, 38}, "Delete", false, H_DELETE, id);
    button(v, c, (ui_rect){304, y + 46, 32, 38}, "↑", false, H_UP, id);
    button(v, c, (ui_rect){344, y + 46, 32, 38}, "↓", false, H_DOWN, id);
  }
  c->text(m->db.error[0] ? m->db.error : "Tap a title to edit · drag ≡ to reorder", 19,
          c->height - 26, 11, m->db.error[0] ? 0xffb4a9ff : muted, 363);
}
void task_screen_down(task_screen *v, float x, float y) {
  v->down_x = x;
  v->down_y = v->last_y = y;
  v->scrolling = v->reordering = false;
  v->dragged = 0;
  v->pressed = (task_hit){0};
  v->in_list = ui_contains(v->list, x, y);
  if (v->in_list && x >= 348) {
    size_t i = (size_t)maximum(0, (y - v->list.y + v->scroll) / 58);
    if (i < tasks_count(v->model)) {
      v->dragged = tasks_ids(v->model)[i];
      v->reordering = true;
    }
    return;
  }
  for (size_t i = v->hit_count; i > 0; i--)
    if (ui_contains(v->hits[i - 1].bounds, x, y)) {
      v->pressed = v->hits[i - 1];
      break;
    }
}
void task_screen_move(task_screen *v, float x, float y) {
  if (v->in_list && !v->reordering &&
      (v->scrolling || fabsf(y - v->down_y) > 6 || fabsf(x - v->down_x) > 6)) {
    v->scrolling = true;
    v->pressed = (task_hit){0};
    v->scroll = (float)clamp(v->scroll + v->last_y - y, 0, v->max_scroll);
  }
  v->last_y = y;
}
void task_screen_up(task_screen *v, float x, float y, bool cancel) {
  task_hit h = v->pressed;
  v->pressed = (task_hit){0};
  if (!cancel && v->reordering && v->dragged)
    tasks_move(v->model, v->dragged, (size_t)maximum(0, (y - v->list.y + v->scroll) / 58));
  else if (!cancel && !v->scrolling && h.action && fabsf(y - v->down_y) < 8 &&
           fabsf(x - v->down_x) < 8)
    act(v, h, (ui_point){x, y});
  v->reordering = v->scrolling = v->in_list = false;
  v->dragged = 0;
}
