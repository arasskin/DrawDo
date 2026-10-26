#include "profiling/task_benchmark.h"
#include "platform/runtime.h"
static void collect(profile_session *p) {
  for (size_t i = p->size > 12 ? p->size - 12 : 0; i < p->size; i++)
    presentation_collect(&p->rows[i].stamp);
}
static bool finish(profile_session *p, task_model *m) {
  if (p->size != (size_t)(p->warmup + p->frames + 12) || m->db.error[0])
    return ui_error(&m->db, "Profile frame coverage or model error");
  if ((p->kind == 1 && m->list_builds != p->list_start) ||
      (p->kind == 2 &&
       (m->list_builds != p->list_start || m->counter_builds != p->counter_start)) ||
      (p->kind == 4 && (m->row_builds != p->row_start || m->counter_builds != p->counter_start)))
    return ui_error(&m->db, "Profile selective-publication regression");
  df_match q[] = {{df_var(0), A_KIND, df_const(df_string_z("task")), true},
                  {df_var(0), A_TITLE, df_var(1), true},
                  {df_var(0), A_DONE, df_var(2), true},
                  {df_var(0), A_ORDER, df_var(3), true}};
  df_watch *w = ui_subscribe(&m->db, q, 4, false, NULL, NULL);
  df_entities ids = {0};
  app_buffer path = {0};
  bool valid =
      w && ui_ids(w->result, 0, &ids) && ids.count == (size_t)(p->count + (p->added ? 1 : 0));
  ui_unwatch(&m->db, &w);
  df_entities_free(&ids);
  if (!valid)
    return ui_error(&m->db, "Profile task count mismatch");
  if (!app_format(&path, "%s.tmp", app_cstr(&p->output)))
    return false;
  FILE *f = fopen(app_cstr(&path), "w");
  if (!f) {
    app_buffer_free(&path);
    return ui_error(&m->db, "Profile output unavailable");
  }
  fprintf(f,
          "{\"count\":%d,\"case\":%d,\"warmup\":%d,\"frames\":%d,\"egl_timestamps\":%d,"
          "\"presentation_timestamps\":%d,\"checks\":\"passed\",\"rows\":[",
          p->count, p->kind, p->warmup, p->frames,
#ifdef __ANDROID__
          p->presentation,
#else
          0,
#endif
          p->presentation);
  bool first = true;
  for (size_t i = 0; i < p->size; i++) {
    profile_row *r = &p->rows[i];
    if (r->frame < p->warmup || r->frame >= p->warmup + p->frames)
      continue;
    fprintf(f,
            "%s{\"frame\":%d,\"action\":%d,\"start_ns\":%lld,\"interval_ns\":%lld,\"action_ns\":%"
            "lld,\"prepare_ns\":%lld,\"submit_ns\":%lld,\"total_ns\":%lld,\"cpu_ns\":%lld",
            first ? "" : ",", r->frame, r->action, (long long)r->start, (long long)r->interval,
            (long long)r->action_ns, (long long)r->prepare_ns, (long long)r->submit_ns,
            (long long)r->total_ns, (long long)r->cpu_ns);
    first = false;
#define FIELD(name) fprintf(f, ",\"" #name "_ns\":%lld", (long long)r->phases.name)
    FIELD(encode);
    FIELD(engine);
    FIELD(publish);
    FIELD(subscribe);
    FIELD(unsubscribe);
    FIELD(text);
    FIELD(raster);
    FIELD(upload);
    FIELD(geometry_upload);
    FIELD(shape_submit);
    FIELD(label_upload);
    FIELD(label_submit);
    FIELD(commit);
    FIELD(diagnostics);
    FIELD(jni_attach);
    FIELD(jni_detach);
    FIELD(java_raster);
    FIELD(bitmap_copy);
    FIELD(pass_begin);
    FIELD(shape_draw);
#undef FIELD
#define VALUE(name) fprintf(f, ",\"" #name "\":%llu", (unsigned long long)r->phases.name)
    VALUE(edits);
    VALUE(subscriptions);
    VALUE(unsubscriptions);
    VALUE(text_misses);
    VALUE(texture_bytes);
    VALUE(texture_entries);
    VALUE(jni_attachments);
    VALUE(labels);
#undef VALUE
    fprintf(f,
            ",\"frame_id\":%llu,\"queue_ns\":%lld,\"gpu_done_ns\":%lld,\"latch_ns\":%lld,\"present_"
            "ns\":%lld,\"list_builds\":%llu,\"row_builds\":%llu,\"counter_builds\":%llu,\"detail_"
            "builds\":%llu}",
            (unsigned long long)r->stamp.id, (long long)r->stamp.queue,
            (long long)r->stamp.gpu_done, (long long)r->stamp.latch, (long long)r->stamp.present,
            (unsigned long long)r->list_builds, (unsigned long long)r->row_builds,
            (unsigned long long)r->counter_builds, (unsigned long long)r->detail_builds);
  }
  fputc(']', f);
  const char *token = getenv("DATAFRIEND_PROFILE_ID");
  if (token && strspn(token, "0123456789abcdef") == strlen(token))
    fprintf(f, ",\"run_id\":\"%s\"", token);
  platform_profile_metadata(f);
  fputs("}\n", f);
  bool ok = !ferror(f);
  if (fclose(f))
    ok = false;
  if (ok)
    ok = rename(app_cstr(&path), app_cstr(&p->output)) == 0;
  app_buffer_free(&path);
  if (!ok)
    return ui_error(&m->db, "Profile output failed");
  p->done = true;
  profile_enabled = false;
  platform_log(false, "TaskProfile", "Complete: count=%d case=%d frames=%d", p->count, p->kind,
               p->frames);
  return true;
}
bool profile_configure(profile_session *p, const char *directory) {
  memset(p, 0, sizeof(*p));
  p->count = 12;
  p->warmup = 60;
  p->frames = 360;
  p->frame = -1;
  p->active = platform_profile_options(&p->count, &p->kind, &p->warmup, &p->frames);
  if (!p->active)
    return true;
  if (p->count < 12 || p->count > 10000 || p->kind < 0 || p->kind > 7 || p->warmup < 10 ||
      p->frames < 30 || p->frames > 10000)
    return false;
  if (!app_format(&p->output, "%s/task-profile.json", directory) ||
      !app_reserve(&p->rows, &p->capacity, (size_t)(p->warmup + p->frames + 12), sizeof(*p->rows)))
    return false;
  unlink(app_cstr(&p->output));
  p->presentation = presentation_init();
  return true;
}
static void ignore_editor(const char *t, ui_rect r, const ui_point *point, ui_text_callback cb,
                          void *ctx) {
  (void)t;
  (void)r;
  (void)point;
  (void)cb;
  (void)ctx;
}
bool profile_seed(profile_session *p, task_model *m, task_screen *v) {
  if (!p->active)
    return true;
  if (!tasks_seed(m, p->count))
    return false;
  p->first = tasks_ids(m)[0];
  v->edit_text = ignore_editor;
  return true;
}
static void tap(task_screen *v, float x, float y) {
  task_screen_down(v, x, y);
  task_screen_up(v, x, y, false);
}
bool profile_begin(profile_session *p, task_model *m, task_screen *v) {
  if (!p->active || p->done)
    return false;
  collect(p);
  if (p->frame + 1 >= p->warmup + p->frames + 12) {
    if (!finish(p, m)) {
      platform_log(true, "TaskProfile", "%s", m->db.error);
      p->done = true;
      profile_enabled = false;
    }
    return false;
  }
  p->frame++;
  profile_current = (profile_metrics){0};
  profile_enabled = true;
  p->current = (profile_row){0};
  p->current.frame = p->frame;
  p->current.start = app_now();
  p->current.interval = p->previous ? p->current.start - p->previous : 0;
  p->previous = p->current.start;
  p->cpu_start = app_clock(CLOCK_THREAD_CPUTIME_ID);
  presentation_begin(&p->current.stamp);
  if (p->frame == p->warmup) {
    p->list_start = m->list_builds;
    p->row_start = m->row_builds;
    p->counter_start = m->counter_builds;
    p->detail_start = m->detail_builds;
  }
  int64_t start = app_now();
  bool action = p->frame >= 10 && p->frame < p->warmup + p->frames && p->frame % 6 == 0;
  if (action) {
    switch (p->kind) {
    case 1:
      tap(v, 44, 259);
      break;
    case 2:
      tasks_rename(m, p->first,
                   p->frame % 12 ? "A title edited by the UI" : "A title changed once more");
      break;
    case 3:
      tap(v, p->frame % 12 ? 280 : 50, 197);
      break;
    case 4:
      task_screen_down(v, 360, 259);
      task_screen_move(v, 360, 317);
      task_screen_up(v, 360, 317, false);
      break;
    case 6:
      if (p->added) {
        ui_delete(&m->db, p->added);
        p->added = 0;
      } else
        p->added = tasks_add(m, "A new task from the editor");
      break;
    default:
      break;
    }
    if (p->kind != 0 && p->kind != 5)
      p->current.action = 1;
  }
  if ((p->kind == 5 || p->kind == 7) && p->frame >= 10 && p->frame < p->warmup + p->frames) {
    if (p->frame == 10)
      task_screen_down(v, 100, 500);
    int phase = (p->frame - 10) % 240;
    float y = phase < 120 ? 500.0f - phase * 3 : 140.0f + (phase - 120) * 3;
    if (p->kind == 7)
      y = 500.0f - (p->frame - 10) * 12;
    task_screen_move(v, 100, y);
    p->current.action = 1;
  }
  p->current.action_ns = app_now() - start;
  return true;
}
void profile_track(profile_session *p, const void *raw) {
  if (p->active && !p->done)
    presentation_track(&p->current.stamp, raw);
}
void profile_finish_frame(profile_session *p, task_model *m, int64_t prepare, int64_t submit) {
  p->current.prepare_ns = prepare;
  p->current.submit_ns = submit;
  p->current.total_ns = app_now() - p->current.start;
  p->current.cpu_ns = app_clock(CLOCK_THREAD_CPUTIME_ID) - p->cpu_start;
  p->current.phases = profile_current;
  p->current.list_builds = m->list_builds;
  p->current.row_builds = m->row_builds;
  p->current.counter_builds = m->counter_builds;
  p->current.detail_builds = m->detail_builds;
  if (p->size < p->capacity)
    p->rows[p->size++] = p->current;
  else {
    presentation_release(&p->current.stamp);
    ui_error(&m->db, "Profile row capacity exceeded");
  }
}
void profile_free(profile_session *p) {
  for (size_t i = 0; i < p->size; i++)
    presentation_release(&p->rows[i].stamp);
  free(p->rows);
  app_buffer_free(&p->output);
  memset(p, 0, sizeof(*p));
  profile_enabled = false;
}
