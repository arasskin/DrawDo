#include "particle_model.h"
static const int counts[] = {100, 300, 1000, 3000};
int particles_cell(double x, double y) { return (int)(y / 80) * 8 + (int)(x / 80); }
static double wrap(double x, double limit) {
  double r = fmod(x, limit);
  return r < 0 ? r + limit : r;
}
static double real(df_view *r, uint32_t id, uint32_t a) {
  df_slice b;
  double v = NAN;
  if (df_view_get(r, id, a, &b) == DF_OK)
    df_value_real(b, &v);
  return v;
}
static void active(particle_app *a) {
  for (int i = 0; i < PARTICLE_CELLS; i++) {
    int dx = abs(i % 8 - a->column), dy = abs(i / 8 - a->row);
    a->cells[i].active = !a->local || (minimum(dx, 8 - dx) <= 1 && minimum(dy, 12 - dy) <= 1);
  }
}
static void refresh(void *ctx, df_view *r) {
  particle_cell *c = ctx;
  particle_app *a = c->owner;
  df_buffer changes = {0};
  if (df_result_changes(r, &changes)) {
    df_free(changes.data);
    ui_error(&a->db, "Cell change read failed");
    return;
  }
  app_reader in = {changes.data, changes.size, false};
  bool reset = app_read(&in, 1) != 0, membership = app_read(&in, 1) != 0;
  size_t changed = (size_t)app_read(&in, 4);
  df_entities ids = {0};
  const uint8_t *changed_ids = app_take(&in, changed * 4);
  if (reset)
    c->count = 0;
  if (reset || membership || !c->watch) {
    if (!ui_ids(r, 0, &ids)) {
      ui_error(&a->db, "Cell membership read failed");
      goto done;
    }
    for (size_t i = 0; i < c->count;) {
      df_buffer b;
      if (!ui_result_bytes(r, c->items[i].id, 8, &b)) {
        memmove(c->items + i, c->items + i + 1, (--c->count - i) * sizeof(*c->items));
      } else
        i++;
    }
  }
  /* Reset/new members need reads even when absent from the change summary. */
  for (size_t pass = 0; pass < 2; pass++) {
    size_t n = pass ? ids.count : changed;
    for (size_t j = 0; j < n; j++) {
      uint32_t id = pass ? ((uint32_t *)ids.data)[j] : (uint32_t)app_be(changed_ids + j * 4, 4);
      size_t index = 0;
      while (index < c->count && c->items[index].id != id)
        index++;
      if (pass && index < c->count)
        continue;
      df_buffer b;
      if (!ui_result_bytes(r, id, 8, &b))
        continue;
      if (index == c->count) {
        if (!app_reserve(&c->items, &c->capacity, c->count + 1, sizeof(*c->items))) {
          ui_error(&a->db, "Cell allocation failed");
          goto done;
        }
        c->count++;
      }
      c->items[index] = (particle){id, real(r, id, 8), real(r, id, 9)};
      a->reads++;
    }
  }
  a->refreshed++;
done:
  df_entities_free(&ids);
  df_free(changes.data);
}
static void settings(void *ctx, df_view *r) {
  particle_app *a = ctx;
  a->count = (int)ui_integer(r, PARTICLE_CONTROL, 26);
  a->paused = ui_flag(r, PARTICLE_CONTROL, 27);
  a->local = ui_flag(r, PARTICLE_CONTROL, 28);
  a->column = (int)ui_integer(r, PARTICLE_CONTROL, 29);
  a->row = (int)ui_integer(r, PARTICLE_CONTROL, 30);
  a->automatic = ui_flag(r, PARTICLE_CONTROL, 31);
  a->status = a->paused  ? "Paused - tap Resume to continue"
              : a->local ? "Drag the active patch"
                         : "Every particle moves each tick";
  a->builds++;
  active(a);
}
static void button_build(void *ctx, df_view *r) {
  particle_button *b = ctx;
  int i = b->index;
  b->builds++;
  if (i < 4)
    b->selected = ui_integer(r, PARTICLE_CONTROL, 26) == counts[i];
  else if (i < 6)
    b->selected = ui_flag(r, PARTICLE_CONTROL, 28) == (i == 4);
  else {
    b->selected = ui_flag(r, PARTICLE_CONTROL, 27);
    snprintf(b->label, sizeof(b->label), "%s", b->selected ? "Resume" : "Pause");
  }
}
/* mt19937_64, preserving the original C++ demo's seed and distribution. */
typedef struct random64 {
  uint64_t words[312];
  size_t index;
} random64;
static void random_seed(random64 *r) {
  r->words[0] = 74291;
  for (size_t i = 1; i < 312; i++)
    r->words[i] = UINT64_C(6364136223846793005) * (r->words[i - 1] ^ (r->words[i - 1] >> 62)) + i;
  r->index = 312;
}
static uint64_t random_next(random64 *r) {
  if (r->index == 312) {
    for (size_t i = 0; i < 312; i++) {
      uint64_t x = (r->words[i] & UINT64_C(0xffffffff80000000)) |
                   (r->words[(i + 1) % 312] & UINT64_C(0x7fffffff));
      r->words[i] =
          r->words[(i + 156) % 312] ^ (x >> 1) ^ ((x & 1) ? UINT64_C(0xb5026f5aa96619e9) : 0);
    }
    r->index = 0;
  }
  uint64_t x = r->words[r->index++];
  x ^= (x >> 29) & UINT64_C(0x5555555555555555);
  x ^= (x << 17) & UINT64_C(0x71d67fffeda60000);
  x ^= (x << 37) & UINT64_C(0xfff7eee000000000);
  return x ^ (x >> 43);
}
static double uniform(random64 *r, double limit) {
  double x = (double)random_next(r) / 18446744073709551616.0;
  if (x >= 1)
    x = nextafter(1, 0);
  return x * limit;
}
bool particles_count(particle_app *a, int n) {
  bool valid = false;
  for (size_t i = 0; i < COUNT(counts); i++)
    if (n == counts[i])
      valid = true;
  if (!valid)
    return ui_error(&a->db, "Unsupported particle count");
  double *drift = calloc((size_t)(n + 1) * 2, sizeof(double));
  if (!drift)
    return ui_error(&a->db, "Drift allocation failed");
  df_edit e = {0};
  for (size_t i = 0; i < PARTICLE_CELLS; i++)
    for (size_t j = 0; j < a->cells[i].count; j++) {
      particle p = a->cells[i].items[j];
      df_edit_retract(&e, p.id, 8, df_real(p.x));
      df_edit_retract(&e, p.id, 9, df_real(p.y));
      df_edit_retract(&e, p.id, 22, df_int(particles_cell(p.x, p.y)));
    }
  random64 rng;
  random_seed(&rng);
  for (int i = 0; i < n; i++) {
    uint32_t id = (uint32_t)i + 1;
    double x = uniform(&rng, 640), y = uniform(&rng, 960), angle = (i + 1) * 2.399963229728653;
    df_edit_set(&e, id, 8, df_real(x));
    df_edit_set(&e, id, 9, df_real(y));
    df_edit_set(&e, id, 22, df_int(particles_cell(x, y)));
    drift[id * 2] = cos(angle) * 45;
    drift[id * 2 + 1] = sin(angle) * 45;
  }
  df_edit_set(&e, PARTICLE_CONTROL, 26, df_int(n));
  bool ok = ui_change(&a->db, &e);
  if (!ok) {
    free(drift);
    return false;
  }
  free(a->drift);
  a->drift = drift;
  a->drift_capacity = (size_t)(n + 1) * 2;
  a->count = n;
  a->tick = 0;
  a->last = (particle_sample){0};
  return true;
}
bool particles_open(particle_app *a, const char *directory) {
  memset(a, 0, sizeof(*a));
  if (!ui_open(&a->db, directory, "default", 2, (size_t)1 << 30))
    return false;
  if (!particles_count(a, 1000))
    return false;
  df_edit e = {0};
  df_edit_set(&e, PARTICLE_CONTROL, 27, df_bool(false));
  df_edit_set(&e, PARTICLE_CONTROL, 28, df_bool(true));
  df_edit_set(&e, PARTICLE_CONTROL, 29, df_int(3));
  df_edit_set(&e, PARTICLE_CONTROL, 30, df_int(5));
  df_edit_set(&e, PARTICLE_CONTROL, 31, df_bool(true));
  if (!ui_change(&a->db, &e))
    return false;
  for (int i = 0; i < PARTICLE_CELLS; i++) {
    particle_cell *c = &a->cells[i];
    c->owner = a;
    df_match q[] = {{df_var(0), 22, df_const(df_int(i)), false},
                    {df_var(0), 8, df_var(1), true},
                    {df_var(0), 9, df_var(2), true}};
    c->watch = ui_subscribe(&a->db, q, 3, false, refresh, c);
    if (!c->watch)
      return false;
  }
  df_match q[6];
  for (int i = 0; i < 6; i++)
    q[i] = (df_match){df_id(PARTICLE_CONTROL), (uint32_t)(26 + i), df_var(i), true};
  a->settings = ui_subscribe(&a->db, q, 6, false, settings, a);
  for (int i = 0; i < 7; i++) {
    particle_button *b = &a->buttons[i];
    b->index = i;
    b->owner = a;
    if (i < 4)
      snprintf(b->label, sizeof(b->label), "%d", counts[i]);
    else
      snprintf(b->label, sizeof(b->label), "%s",
               i == 4   ? "Local patch"
               : i == 5 ? "All moving"
                        : "Pause");
    df_match c = {df_id(PARTICLE_CONTROL), i < 4 ? 26 : i < 6 ? 28 : 27, df_var(0), true};
    b->watch = ui_subscribe(&a->db, &c, 1, false, button_build, b);
    if (!b->watch)
      return false;
  }
  return a->settings && !a->db.error[0];
}
void particles_close(particle_app *a) {
  ui_close(&a->db);
  for (size_t i = 0; i < PARTICLE_CELLS; i++)
    free(a->cells[i].items);
  free(a->drift);
  free(a->updates);
  free(a->neighbors);
  memset(a, 0, sizeof(*a));
}
bool particles_press(particle_app *a, int i) {
  if (i < 0 || i >= 7)
    return false;
  if (i < 4)
    return particles_count(a, counts[i]);
  df_edit e = {0};
  df_edit_set(&e, PARTICLE_CONTROL, i == 6 ? 27 : 28, df_bool(i == 6 ? !a->paused : i == 4));
  return ui_change(&a->db, &e);
}
bool particles_patch(particle_app *a, int col, int row) {
  df_edit e = {0};
  df_edit_set(&e, PARTICLE_CONTROL, 29, df_int((int)clamp(col, 0, 7)));
  df_edit_set(&e, PARTICLE_CONTROL, 30, df_int((int)clamp(row, 0, 11)));
  df_edit_set(&e, PARTICLE_CONTROL, 31, df_bool(false));
  return ui_change(&a->db, &e);
}
uint64_t particles_button_builds(particle_app *a) {
  uint64_t n = 0;
  for (size_t i = 0; i < 7; i++)
    n += a->buttons[i].builds;
  return n;
}
bool particles_step(particle_app *a) {
  if (a->paused)
    return true;
  int64_t start = app_us();
  df_edit e = {0};
  if (a->local && a->automatic && a->tick > 0 && a->tick % 90 == 0) {
    a->column = (a->column + 1) % 8;
    a->row = (a->row + (a->column == 0)) % 12;
    df_edit_set(&e, PARTICLE_CONTROL, 29, df_int(a->column));
    df_edit_set(&e, PARTICLE_CONTROL, 30, df_int(a->row));
  }
  active(a);
  size_t updated = 0;
  particle_sample sample = {0};
  if (!app_reserve(&a->updates, &a->update_capacity, (size_t)a->count, sizeof(particle_update)))
    goto oom;
  for (int i = 0; i < PARTICLE_CELLS; i++) {
    particle_cell *c = &a->cells[i];
    if (!c->active)
      continue;
    size_t neighbors = 0;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        particle_cell *other = &a->cells[((i / 8 + dy + 12) % 12) * 8 + (i % 8 + dx + 8) % 8];
        if (!app_reserve(&a->neighbors, &a->neighbor_capacity, neighbors + other->count,
                         sizeof(particle)))
          goto oom;
        if (other->count)
          memcpy(a->neighbors + neighbors, other->items, other->count * sizeof(particle));
        neighbors += other->count;
      }
    for (size_t j = 0; j < c->count; j++) {
      particle p = c->items[j];
      double fx = 0, fy = 0;
      for (size_t k = 0; k < neighbors; k++) {
        particle other = a->neighbors[k];
        if (p.id == other.id)
          continue;
        sample.pairs++;
        double dx = p.x - other.x, dy = p.y - other.y;
        if (dx > 320)
          dx -= 640;
        if (dx < -320)
          dx += 640;
        if (dy > 480)
          dy -= 960;
        if (dy < -480)
          dy += 960;
        double squared = dx * dx + dy * dy;
        if (squared >= 576 || squared < 1e-12)
          continue;
        sample.interactions++;
        double distance = sqrt(squared), force = (1 - distance / 24) * 80 / distance;
        fx += dx * force;
        fy += dy * force;
      }
      double x = wrap(p.x + clamp((a->drift[p.id * 2] + fx) / 60, -10, 10), 640),
             y = wrap(p.y + clamp((a->drift[p.id * 2 + 1] + fy) / 60, -10, 10), 960);
      bool crossing = particles_cell(x, y) != i;
      a->updates[updated++] = (particle_update){{p.id, x, y}, crossing};
      sample.crossings += crossing;
    }
  }
  sample.physics = app_us() - start;
  for (size_t i = 0; i < updated; ++i) {
    particle p = a->updates[i].value;
    df_edit_set(&e, p.id, 8, df_real(p.x));
    df_edit_set(&e, p.id, 9, df_real(p.y));
    if (a->updates[i].crossing)
      df_edit_set(&e, p.id, 22, df_int(particles_cell(p.x, p.y)));
  }
  sample.prepare = app_us() - start - sample.physics;
  a->reads = a->refreshed = 0;
  int64_t edited = app_us();
  if (!ui_change(&a->db, &e))
    return false;
  sample.edit = app_us() - edited;
  sample.total = app_us() - start;
  sample.moved = (int)updated;
  sample.changed = a->refreshed;
  sample.reads = a->reads;
  a->last = sample;
  a->tick++;
  return !a->db.error[0];
oom:
  df_edit_free(&e);
  return ui_error(&a->db, "Simulation allocation failed");
}
bool particles_validate(particle_app *a) {
  df_watch *all = ui_subscribe(&a->db, NULL, 0, true, NULL, NULL);
  if (!all)
    return false;
  bool *seen = calloc((size_t)a->count + 1, sizeof(bool));
  bool ok = seen && df_view_size(all->result) == (size_t)a->count * 3 + 6;
  size_t count = 0;
  for (int i = 0; i < PARTICLE_CELLS; i++) {
    particle_cell *c = &a->cells[i];
    ok = ok && df_view_size(c->watch->result) == c->count * 2;
    for (size_t j = 0; j < c->count; j++) {
      particle p = c->items[j];
      if (!p.id || p.id > (uint32_t)a->count || !seen || seen[p.id]) {
        ok = false;
        continue;
      }
      seen[p.id] = true;
      count++;
      ok = ok && isfinite(p.x) && isfinite(p.y) && particles_cell(p.x, p.y) == i &&
           real(all->result, p.id, 8) == p.x && real(all->result, p.id, 9) == p.y &&
           ui_integer(all->result, p.id, 22) == i;
    }
  }
  free(seen);
  ui_unwatch(&a->db, &all);
  return ok && count == (size_t)a->count;
}
