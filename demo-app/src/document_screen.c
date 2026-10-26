#include "document_screen.h"
void document_screen_init(document_screen *v, document_model *m) {
  memset(v, 0, sizeof(*v));
  v->model = m;
}
void document_screen_free(document_screen *v) {
  sequence_close(&v->draft);
  app_buffer_free(&v->status);
  memset(v, 0, sizeof(*v));
}
static void submitted(void *ctx, const char *text) {
  document_screen *v = ctx;
  if (v->drafting)
    document_replace_draft(v->model, &v->draft, text);
}
void document_screen_press(document_screen *v, float x, float y) {
  document_model *m = v->model;
  df_buffer text;
  if (!document_text(m, &text))
    return;
  app_buffer next = {0};
  for (int i = 0; i < 5; i++)
    if (ui_contains(v->hits[i], x, y)) {
      if (i == 0 || i == 4) {
        sequence_close(&v->draft);
        v->drafting = sequence_fork(&m->doc, &v->draft);
        if (v->drafting && v->edit_text && app_string(&next, (const char *)text.data, text.size))
          v->edit_text(app_cstr(&next), v->edit_bounds, i == 4 ? &(ui_point){x, y} : NULL,
                       submitted, v);
      } else if (i == 1) {
        if (app_append(&next, text.data, text.size) &&
            app_append(&next, m->config.role ? " blue" : " green", m->config.role ? 5 : 6))
          document_replace(m, app_cstr(&next));
      } else if (i == 2)
        document_online(m, !m->online);
      else {
        app_buffer offsets = {0};
        if (utf8_offsets((const char *)text.data, text.size, &offsets)) {
          size_t n = offsets.size / sizeof(size_t) - 1;
          size_t bytes = n ? ((size_t *)offsets.data)[n - 1] : 0;
          if (app_string(&next, (const char *)text.data, bytes))
            document_replace(m, app_cstr(&next));
        }
        app_buffer_free(&offsets);
      }
      break;
    }
  app_buffer_free(&next);
}
void document_screen_draw(document_screen *v, ui_surface *c) {
  document_model *m = v->model;
  c->clip((ui_rect){0, 0, c->width, c->height});
  c->text("Shared document", 18, 24, 26, 0xe8f2f7ff, 270);
  if (app_us() - v->status_at > 500000) {
    v->status_at = app_us();
    const df_network_stats *s = df_network_statistics(m->connection);
    app_format(&v->status, "%s   %llu B",
               !m->online                            ? "Offline · edits stay on this phone"
               : df_network_connected(m->connection) ? "Connected · signed owner writes"
                                                     : "Connecting to Littlebear",
               (unsigned long long)(s->sent + s->received));
  }
  c->text(app_cstr(&v->status), 18, 67, 12, 0x70e7baff, 364);
  c->text(m->config.role ? "Phone B · independent writer" : "Phone A · independent writer", 18, 88,
          12, 0x87a5b6ff, 364);
  ui_rect rects[] = {
      {18, 116, 112, 38}, {140, 116, 116, 38}, {266, 116, 116, 38}, {18, 164, 112, 36}};
  const char *labels[] = {"Type / edit", "Insert word", m->online ? "Go offline" : "Reconnect",
                          "Delete last"};
  for (int i = 0; i < 4; i++) {
    v->hits[i] = rects[i];
    ui_access(c, 10 + (uint64_t)i, rects[i], labels[i], "", ACCESS_BUTTON);
    c->shape(rects[i], 0x20323fff, false);
    c->text(labels[i], rects[i].x + 8, rects[i].y + 10, 13, 0x70e7baff, rects[i].w - 16);
  }
  c->text(m->done              ? "Benchmark complete"
          : m->config.start_ms ? "Scripted sync measurement"
                               : "Edit either phone; changes merge here",
          142, 174, 11, 0x87a5b6ff, 238);
  c->shape((ui_rect){18, 217, 364, (float)maximum(90, c->height - 300)}, 0x14232eff, false);
  v->edit_bounds = (ui_rect){28, 230, 344, (float)maximum(64, c->height - 326)};
  v->hits[4] = v->edit_bounds;
  df_buffer text;
  if (!document_text(m, &text))
    return;
  app_buffer offsets = {0}, line = {0};
  if (!utf8_offsets((const char *)text.data, text.size, &offsets))
    goto done;
  size_t count = offsets.size / sizeof(size_t) - 1;
  /* Keep the canvas backing visible while the native window is opening or
   * closing. The opaque editor covers it once its own surface is ready. */
  if (!app_string(&line, (const char *)text.data, text.size))
    goto done;
  c->paragraph(text.size ? app_cstr(&line) : "Your shared document starts here.", v->edit_bounds,
               16, 0xe8f2f7ff);
  ui_access(c, 14, v->edit_bounds, "Edit document", app_cstr(&line), ACCESS_BUTTON);
  app_format(&line, "%zu characters · %llu document edits", count,
             (unsigned long long)(m->local_tick + m->peer_tick));
  c->text(app_cstr(&line), 18, c->height - 64, 12, 0x87a5b6ff, 364);
  ui_access(c, 15, (ui_rect){18, 67, 364, 18}, app_cstr(&v->status), "", 0);
  const char *error = document_error(m);
  c->text(*error ? error : "App CRDT → Datafriend → Littlebear → peer", 18, c->height - 42, 11,
          *error ? 0xffbb88ff : 0x70e7baff, 364);
done:
  app_buffer_free(&offsets);
  app_buffer_free(&line);
}
