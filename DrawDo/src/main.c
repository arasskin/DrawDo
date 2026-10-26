#include "screen.h"
#include "platform.h"
#include "present.h"
#include "loop.h"
#include "datafriend/network.h"
#include "text_buffers/native.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_log.h"
/* One owner thread orchestrates the database, native mailboxes and cached scene. */
static struct {
  todo_model model;
  todo_screen screen;
  presentation replacement;
  df_store *store;
  df_network *network;
  void *identity;
  struct {
    editor_session editor;
    uint32_t edit_id;
    float down_x, down_y;
    bool pressed;
  } interaction;
  bool ready, foreground, graphics;
} app;
static void fail(const char *message) {
  app.ready = false;
  loop_watch((df_network_watch){-1, 0, UINT64_MAX});
  platform_error(message);
}
static void submitted(void *context, const char *text) {
  (void)context;
  if (app.interaction.edit_id)
    todo_title(&app.model, app.interaction.edit_id, text);
  else
    todo_add(&app.model, text);
  app.model.dirty = true;
}
static void edit(const screen_item *item, float x, float y, bool point) {
  if (!app.model.writable || app.interaction.editor.active || app.interaction.editor.retained)
    return;
  todo_style *s = &app.model.style;
  todo_row *row = item->id ? todo_find(&app.model, item->id) : NULL;
  if (item->id && !row)
    return;
  float scale = app.screen.scale;
  editor_options options = {.bounds = {item->text.x * scale, item->text.y * scale,
                                       item->text.w * scale, item->text.h * scale},
                            .font_pixels = s->font_size * scale,
                            .foreground = row && row->done ? s->done_color : s->text_color,
                            .background = s->background,
                            .multiline = true,
                            .live = false,
                            .retain_on_submit = true,
                            .label = item->id ? "Task title" : "New task",
                            .place_at_point = point,
                            .point_x = x - item->text.x * scale,
                            .point_y = y - item->text.y * scale};
  app.interaction.edit_id = item->id;
  if (!editor_open(&app.interaction.editor, row ? ct_bytes_string(&row->title) : "", options,
                   submitted, NULL))
    fail("Cannot open task editor");
  app.model.dirty = true;
}
static void process(void) {
  if (!app.ready || !app.foreground)
    return;
  bool editing = app.interaction.editor.active;
  editor_poll(&app.interaction.editor);
  if (editing != app.interaction.editor.active)
    app.model.dirty = true;
  if (app.interaction.editor.events & EDITOR_ERROR) {
    fail("Native text editor failed");
    return;
  }
  access_event event;
  while (access_poll(&event)) {
    const access_node *node = access_find(&app.screen.accessibility, event.id);
    if (!node || node->flags & ACCESS_DISABLED || event.action != ACCESS_ACTIVATE ||
        app.interaction.editor.active || app.interaction.editor.retained)
      continue;
    uint32_t id = event.id == 1 ? 0 : (uint32_t)((event.id - 2) / 2);
    for (size_t i = 0; i < app.screen.count; ++i) {
      const screen_item *item = app.screen.items + i;
      if (item->id != id)
        continue;
      if (node->flags & ACCESS_CHECKBOX)
        todo_toggle(&app.model, id);
      else
        edit(item, 0, 0, false);
      break;
    }
  }
  df_network_pump(app.network, 1500);
  if (app.model.error[0]) {
    fail(app.model.error);
    return;
  }
  if (!loop_watch(df_network_prepare(app.network)))
    fail("Cannot watch network events");
}
static bool open_data(const char *directory) {
  df_buffer error = {0};
  const char *alias = "drawdo-app-v1";
  app.identity = df_identity_open((const uint8_t *)alias, strlen(alias), 0, &error);
  if (!app.identity)
    goto failed;
  char owner[65], path[4096];
  const uint8_t *key = df_identity_public(app.identity);
  for (size_t i = 0; i < 32; ++i)
    snprintf(owner + i * 2, 3, "%02x", key[i]);
  if (strlen(directory) + 80 >= sizeof(path)) {
    fail("Application path too long");
    goto done;
  }
  snprintf(path, sizeof(path), "%s/public-key.txt", directory);
  FILE *file = fopen(path, "w");
  if (!file) {
    fail("Cannot export namespace public key");
    goto done;
  }
  bool written = fprintf(file, "%s\n", owner) > 0;
  if (fclose(file) || !written) {
    fail("Cannot export namespace public key");
    goto done;
  }
  snprintf(path, sizeof(path), "%s/own", directory);
  app.store = df_store_open(&(df_options){path, owner, DF_ASYNC, 64u << 20}, &error);
  if (!app.store)
    goto failed;
  if (!todo_open(&app.model, app.store, true)) {
    fail(app.model.error);
    goto done;
  }
  app.network = df_network_open("147.182.181.182", 7777, &error);
  if (!app.network || !df_network_push(app.network, app.store, app.identity, &error))
    goto failed;
  df_free(error.data);
  return true;
failed:
  {
    char message[512];
    snprintf(message, sizeof(message), "Cannot open DrawDo: %.*s", (int)minimum(error.size, 450),
             error.data ? (const char *)error.data : "");
    fail(message);
  }
done:
  df_free(error.data);
  return false;
}
static void init(void) {
  sg_setup(&(sg_desc){.environment = sglue_environment(),
                      .logger.func = slog_func,
                      .image_pool_size = 512,
                      .view_pool_size = 512});
  const char *directory = platform_init();
  if (!directory || !loop_open(process)) {
    platform_error("Cannot initialize application loop");
    return;
  }
  app.foreground = true;
  app.graphics = screen_open();
  if (!app.graphics) {
    fail("Cannot initialize renderer");
    return;
  }
  editor_native_notify(loop_wake);
  access_notify(loop_wake);
  app.ready = open_data(directory);
  process();
}
static void frame(void) {
  if (!app.interaction.editor.retained)
    present_reset(&app.replacement);
  else if (present_ready(&app.replacement)) {
    editor_release(&app.interaction.editor);
    present_reset(&app.replacement);
    app.model.dirty = true;
  }
  float scale = platform_scale();
  editor_rect bounds = platform_bounds();
  if (app.ready && (app.model.dirty || memcmp(&bounds, &app.screen.bounds, sizeof(bounds)) ||
                    scale != app.screen.scale)) {
    if (!screen_build(&app.screen, &app.model, bounds, scale,
                      app.interaction.editor.active || app.interaction.editor.retained))
      fail(app.model.error[0] ? app.model.error : "Cannot build todo screen");
  }
  uint32_t color = app.ready ? app.model.style.background : 0xffffffff;
  sg_swapchain swapchain = sglue_swapchain();
  if (app.ready && app.interaction.editor.retained)
    present_track(&app.replacement, swapchain.metal.current_drawable);
  sg_begin_pass(&(sg_pass){
      .action.colors[0] = {.load_action = SG_LOADACTION_CLEAR,
                           .clear_value = {(color >> 24) / 255.f, ((color >> 16) & 255) / 255.f,
                                           ((color >> 8) & 255) / 255.f, (color & 255) / 255.f}},
      .swapchain = swapchain});
  if (app.ready)
    screen_draw(sapp_widthf() / scale, sapp_heightf() / scale);
  sg_end_pass();
  sg_commit();
}
static void event(const sapp_event *e) {
  if (e->type == SAPP_EVENTTYPE_SUSPENDED) {
    app.foreground = false;
    app.interaction.pressed = false;
    editor_cancel(&app.interaction.editor);
    present_reset(&app.replacement);
    app.model.dirty = true;
    if (app.store && df_store_flush(app.store) != DF_OK)
      fail(df_store_error(app.store));
    loop_watch((df_network_watch){-1, 0, UINT64_MAX});
    return;
  }
  if (e->type == SAPP_EVENTTYPE_RESUMED) {
    app.foreground = true;
    process();
    return;
  }
  if (!app.ready)
    return;
  if (e->type == SAPP_EVENTTYPE_RESIZED) {
    app.model.dirty = true;
    process();
  }
  bool down = e->type == SAPP_EVENTTYPE_TOUCHES_BEGAN || e->type == SAPP_EVENTTYPE_MOUSE_DOWN;
  bool up = e->type == SAPP_EVENTTYPE_TOUCHES_ENDED || e->type == SAPP_EVENTTYPE_MOUSE_UP;
  if (e->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED)
    app.interaction.pressed = false;
  if (!down && !up)
    return;
  float x = e->num_touches ? e->touches[0].pos_x : e->mouse_x;
  float y = e->num_touches ? e->touches[0].pos_y : e->mouse_y;
  if (down) {
    app.interaction.down_x = x;
    app.interaction.down_y = y;
    app.interaction.pressed = true;
    return;
  }
  bool tap = app.interaction.pressed &&
             hypotf(x - app.interaction.down_x, y - app.interaction.down_y) < 12 * platform_scale();
  app.interaction.pressed = false;
  if (tap && !app.interaction.editor.active && !app.interaction.editor.retained) {
    bool checkbox;
    const screen_item *item = screen_hit(&app.screen, x, y, &checkbox);
    if (item) {
      if (checkbox)
        todo_toggle(&app.model, item->id);
      else
        edit(item, x, y, true);
      process();
    }
  }
}
static void cleanup(void) {
  editor_native_notify(NULL);
  access_notify(NULL);
  loop_close();
  editor_free(&app.interaction.editor);
  present_reset(&app.replacement);
  df_network_close(app.network);
  todo_close(&app.model);
  df_buffer error = {0};
  if (app.store)
    df_store_close(app.store, &error);
  df_free(error.data);
  df_identity_close(app.identity);
  if (app.graphics)
    screen_close(&app.screen);
  platform_shutdown();
  sg_shutdown();
}
sapp_desc sokol_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  return (sapp_desc){.init_cb = init,
                     .frame_cb = frame,
                     .event_cb = event,
                     .cleanup_cb = cleanup,
                     .window_title = "DrawDo",
                     .high_dpi = true,
                     .width = 400,
                     .height = 800,
                     .logger.func = slog_func};
}
