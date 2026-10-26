#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_log.h"
#include "render/shaders.h"
#include "render/text.h"
#include "particle_model.h"
#include "task_screen.h"
#include "document_screen.h"
#include "text_buffers/native.h"
#include "platform/runtime.h"
#include "accessible_ui/native.h"
#include "haptics/haptics.h"
#include "profiling/task_benchmark.h"
#include "profiling/document_frames.h"
void demo_trace_frame(void);
typedef struct vertex {
  float x, y, u, v;
  uint32_t color;
  float circle;
} vertex;
static const float ui_width = 400;
static const uint32_t ink = 0xe8f2f7ff, muted = 0x87a5b6ff, mint = 0x70e7baff;
static particle_app particles;
static bool particles_ready;
static task_model tasks;
static task_screen task_view;
static bool tasks_ready;
static document_model document;
static document_screen document_view;
static bool document_ready, document_started;
static void *identity;
static document_frames document_probe;
static int document_drain;
static bool document_saved;
static profile_session profiler;
static bool tasks_mode = true, document_mode, failed, gpu_ready, text_ready;
typedef struct interaction_state {
  editor_session text;
  access_tree access;
  bool dragging;
} interaction_state;
static interaction_state interaction;
static ui_rect draw_clip = {0, 0, 10000, 10000}, board;
static float ui_height = 800, scale = 1, ui_top;
static vertex geometry[50000];
static size_t geometry_count;
static int label_slot;
static sg_buffer buffer;
static sg_pipeline pipeline;
static sg_shader shader;
static sg_pass_action pass_action;
static double fps = 60, elapsed, last_display;
static int frames, display_frames;
static int64_t previous_frame;
static char metrics[160] = "Starting native simulation", workload[160];
static app_buffer directory;
static void error(const char *message) {
  failed = true;
  platform_log(true, "DrawDoSokol", "%s", message);
}
static uint32_t packed(uint32_t c) {
  return (c >> 24) | ((c >> 8) & 0xff00) | ((c << 8) & 0xff0000) | ((c & 255) << 24);
}
static void quad(ui_rect r, uint32_t color, bool circle) {
  ui_rect original = r;
  float right = (float)minimum(r.x + r.w, draw_clip.x + draw_clip.w),
        bottom = (float)minimum(r.y + r.h, draw_clip.y + draw_clip.h);
  r.x = (float)maximum(r.x, draw_clip.x);
  r.y = (float)maximum(r.y, draw_clip.y);
  r.w = right - r.x;
  r.h = bottom - r.y;
  if (r.w <= 0 || r.h <= 0)
    return;
  if (geometry_count + 6 > COUNT(geometry)) {
    error("Geometry capacity exceeded");
    return;
  }
  float u0 = (r.x - original.x) / original.w * 2 - 1, v0 = (r.y - original.y) / original.h * 2 - 1,
        u1 = (right - original.x) / original.w * 2 - 1,
        v1 = (bottom - original.y) / original.h * 2 - 1;
  r.y += ui_top;
  uint32_t c = packed(color);
  float k = circle ? 1 : 0;
  vertex v[] = {{r.x, r.y, u0, v0, c, k},
                {r.x + r.w, r.y, u1, v0, c, k},
                {r.x + r.w, r.y + r.h, u1, v1, c, k},
                {r.x, r.y, u0, v0, c, k},
                {r.x + r.w, r.y + r.h, u1, v1, c, k},
                {r.x, r.y + r.h, u0, v1, c, k}};
  memcpy(geometry + geometry_count, v, sizeof(v));
  geometry_count += 6;
}
static ui_rect button_bounds(size_t i) {
  return i < 4 ? (ui_rect){18.0f + (float)i * 93, 112, 85, 35}
               : (ui_rect){18.0f + (float)(i - 4) * 127, 161, i == 6 ? 110 : 119, 34};
}
static void layout(void) {
  scale = sapp_widthf() / ui_width;
  if (scale <= 0)
    scale = 1;
  platform_insets insets = platform_safe_area();
  ui_top = insets.top / scale;
  ui_height = sapp_heightf() / scale - ui_top - insets.bottom / scale;
  float h = (float)clamp(ui_height - 302, 80, 364 * 1.5);
  board = (ui_rect){(ui_width - h / 1.5f) / 2, 211, h / 1.5f, h};
}
static void label(int slot, float x, float y, uint32_t color) {
  if (!labels_draw(slot, x, y + ui_top, color))
    error("Text draw failed");
}
static void prepare(int slot, const char *text, float size, float width) {
  if (!labels_prepare(slot, text, size, scale, width))
    error("Text raster/cache preparation failed");
}
static void surface_text(const char *text, float x, float y, float size, uint32_t color,
                         float width) {
  prepare(label_slot, text, size, width);
  label(label_slot++, x, y, color);
}
static void surface_clip(ui_rect r) {
  draw_clip = r;
  labels_clip(r.x, r.y + ui_top, r.w, r.h);
}
static void surface_paragraph(const char *text, ui_rect r, float size, uint32_t color) {
  if (!labels_prepare_paragraph(label_slot, text, size, scale, r.w, r.h))
    error("Paragraph raster/cache preparation failed");
  /* Match the integer pixel origin used by the Android native control. */
  if (!labels_draw(label_slot++, floorf(r.x * scale) / scale,
                   floorf((r.y + ui_top) * scale) / scale, color))
    error("Paragraph draw failed");
}
static void surface_access(uint64_t id, ui_rect r, const char *label, const char *value,
                           unsigned flags) {
  if (interaction.text.active)
    return;
  access_add(&interaction.access, id,
             (access_rect){floorf(r.x * scale), floorf((r.y + ui_top) * scale), floorf(r.w * scale),
                           floorf(r.h * scale)},
             label, value, flags);
}
static editor_rect editor_pixels(ui_rect r) {
  return (editor_rect){floorf(r.x * scale), floorf((r.y + ui_top) * scale), floorf(r.w * scale),
                       floorf(r.h * scale)};
}
static editor_options edit_options(ui_rect r, const ui_point *point, bool document) {
  editor_options options = {.bounds = editor_pixels(r),
                            .font_pixels = 16 * scale,
                            .foreground = 0xe8f2f7ff,
                            .background = document ? 0x14232eff : 0x1f363eff,
                            .multiline = document,
                            .live = document,
                            .label = document ? "Shared document" : "Task title"};
  if (point) {
    options.place_at_point = true;
    options.point_x = point->x * scale - options.bounds.x;
    options.point_y = (point->y + ui_top) * scale - options.bounds.y;
  }
  return options;
}
static void edit_text(const char *text, ui_rect r, const ui_point *point, ui_text_callback cb,
                      void *ctx) {
  if (!editor_open(&interaction.text, text, edit_options(r, point, false), cb, ctx))
    ui_error(&tasks.db, "Text editor unavailable");
}
static void edit_document(const char *text, ui_rect r, const ui_point *point, ui_text_callback cb,
                          void *ctx) {
  if (!editor_open(&interaction.text, text, edit_options(r, point, true), cb, ctx))
    snprintf(document.error, sizeof(document.error), "Text editor unavailable");
}
static void poll_editor(void) {
  bool is_document = interaction.text.context == &document_view;
  editor_poll(&interaction.text);
  if (interaction.text.events & EDITOR_ERROR) {
    const char *message = "Text editor stopped: could not accept native input";
    if (is_document)
      snprintf(document.error, sizeof(document.error), "%s", message);
    else
      ui_error(&tasks.db, message);
    platform_log(true, "TextEditor", "%s", message);
  }
}
static void init(void) {
#if defined(__APPLE__)
  editor_apple_init((void *)sapp_ios_get_window());
  access_apple_init((void *)sapp_ios_get_window());
#endif
  previous_frame = app_now();
  sg_desc desc = {0};
  desc.image_pool_size = desc.view_pool_size = LABEL_POOL_SIZE;
  desc.environment = sglue_environment();
  desc.logger.func = slog_func;
  sg_setup(&desc);
  gpu_ready = sg_isvalid();
  if (!gpu_ready) {
    error("Graphics initialization failed");
    return;
  }
  pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
  pass_action.colors[0].clear_value = (sg_color){16 / 255.0f, 25 / 255.0f, 35 / 255.0f, 1};
  sg_buffer_desc bd = {0};
  bd.size = sizeof(geometry);
  bd.usage.vertex_buffer = bd.usage.stream_update = true;
  buffer = sg_make_buffer(&bd);
  sg_pipeline_desc pd = {0};
  shader = sg_make_shader(shape_shader_desc(sg_query_backend()));
  pd.shader = shader;
  pd.layout.attrs[ATTR_shape_pos].format = SG_VERTEXFORMAT_FLOAT2;
  pd.layout.attrs[ATTR_shape_uv0].format = SG_VERTEXFORMAT_FLOAT2;
  pd.layout.attrs[ATTR_shape_color0].format = SG_VERTEXFORMAT_UBYTE4N;
  pd.layout.attrs[ATTR_shape_circle0].format = SG_VERTEXFORMAT_FLOAT;
  pd.colors[0].blend.enabled = true;
  pd.colors[0].blend.src_factor_rgb = pd.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
  pd.colors[0].blend.dst_factor_rgb = pd.colors[0].blend.dst_factor_alpha =
      SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  pipeline = sg_make_pipeline(&pd);
  text_ready = labels_init();
  if (!text_ready) {
    error("Text initialization failed");
    return;
  }
  const char *root = platform_directory();
  if (!root || !app_mkdir(root) || !app_format(&directory, "%s/particle-session", root) ||
      !app_remove_tree(app_cstr(&directory)) || !app_mkdir(app_cstr(&directory)) ||
      !profile_configure(&profiler, root)) {
    error("Application directory/profile initialization failed");
    return;
  }
  app_buffer path = {0};
  if (!app_format(&path, "%s/%s", root, profiler.active ? "tasks-profile" : "tasks-v1")) {
    error("Path allocation failed");
    return;
  }
  if (profiler.active && !app_remove_tree(app_cstr(&path))) {
    app_buffer_free(&path);
    error("Profile directory reset failed");
    return;
  }
  tasks_ready = tasks_open(&tasks, app_cstr(&path));
  app_buffer_free(&path);
  if (!tasks_ready) {
    error(tasks.db.error);
    return;
  }
  task_screen_init(&task_view, &tasks);
  task_view.edit_text = edit_text;
  task_view.session = &interaction.text;
  if (!profile_seed(&profiler, &tasks, &task_view)) {
    error(tasks.db.error);
    return;
  }
  df_buffer reply = {0};
  const char *alias = "document-demo-v1";
  identity = df_identity_open((const uint8_t *)alias, strlen(alias), 0, &reply);
  if (!identity) {
    char message[512];
    snprintf(message, sizeof(message), "Identity: %.*s", (int)minimum(reply.size, 490),
             (const char *)reply.data);
    df_free(reply.data);
    error(message);
    return;
  }
  df_free(reply.data);
  char hex[66];
  const uint8_t *key = df_identity_public(identity);
  for (size_t i = 0; i < 32; i++)
    snprintf(hex + i * 2, 3, "%02x", key[i]);
  hex[64] = '\n';
  hex[65] = 0;
  if (app_format(&path, "%s/document-public-key.txt", root))
    app_write(app_cstr(&path), hex, 65);
  app_buffer_free(&path);
  document_config cfg;
  int configured = profiler.active ? 0 : document_config_load(&cfg, root);
  if (configured < 0) {
    error("Invalid document configuration");
    return;
  }
  if (configured) {
    document_started = true;
    document_ready = document_open(&document, root, &cfg, identity);
    if (!document_ready) {
      error(document.error);
      return;
    }
    document_screen_init(&document_view, &document);
    document_view.edit_text = edit_document;
    document_view.session = &interaction.text;
    presentation_init();
    document_mode = true;
    tasks_mode = false;
    platform_keep_awake(true);
  }
  layout();
  platform_log(false, "DrawDoSokol", "Ready: C99 tasks with Datafriend row views");
}
static void present(void) {
  sg_range data = {geometry, geometry_count * sizeof(vertex)};
  int64_t t = profile_start();
  sg_update_buffer(buffer, &data);
  profile_end(&profile_current.geometry_upload, t);
  t = profile_start();
  sg_pass pass = {0};
  pass.action = pass_action;
  pass.swapchain = sglue_swapchain();
  profile_track(&profiler, pass.swapchain.metal.current_drawable);
  if (document_mode)
    document_frames_track(&document_probe, pass.swapchain.metal.current_drawable);
  int64_t begin = profile_start();
  sg_begin_pass(&pass);
  profile_end(&profile_current.pass_begin, begin);
  int64_t drawing = profile_start();
  sg_apply_scissor_rect(0, 0, sapp_width(), sapp_height(), true);
  sg_apply_pipeline(pipeline);
  sg_bindings bind = {0};
  bind.vertex_buffers[0] = buffer;
  sg_apply_bindings(&bind);
  shape_params_t params = {0};
  params.fb_size[0] = ui_width;
  params.fb_size[1] = sapp_heightf() / scale;
  sg_apply_uniforms(UB_shape_params, &SG_RANGE(params));
  sg_draw(0, (int)geometry_count, 1);
  profile_end(&profile_current.shape_draw, drawing);
  profile_end(&profile_current.shape_submit, t);
  labels_flush(ui_width, sapp_heightf() / scale);
  t = profile_start();
  sg_end_pass();
  demo_trace_frame();
  sg_commit();
  profile_end(&profile_current.commit, t);
}
static ui_surface surface(void) {
  return (ui_surface){ui_width,     ui_height,         quad,          surface_text,
                      surface_clip, surface_paragraph, surface_access};
}
static void task_frame(void) {
  bool measuring = profile_begin(&profiler, &tasks, &task_view);
  int64_t prepare_start = measuring ? app_now() : 0;
  poll_editor();
  layout();
  geometry_count = 0;
  label_slot = 0;
  ui_surface c = surface();
  task_screen_draw(&task_view, &c);
  if (interaction.text.active && interaction.text.context == &task_view) {
    if (task_view.edit_visible)
      editor_layout(&interaction.text, editor_pixels(task_view.edit_bounds));
    else
      editor_cancel(&interaction.text);
  }
  quad((ui_rect){302, 22, 80, 30}, 0x20323fff, false);
  prepare(90, "Document", 11, 0);
  label(90, 308, 30, mint);
  surface_access(1, (ui_rect){302, 22, 80, 30}, "Document", "", ACCESS_BUTTON);
  int64_t submit_start = measuring ? app_now() : 0;
  present();
  int64_t submit_end = measuring ? app_now() : 0;
  int64_t t = profile_start();
  uint64_t revision = df_revision(tasks.db.engine);
  profile_end(&profile_current.diagnostics, t);
  if (measuring)
    profile_finish_frame(&profiler, &tasks, submit_start - prepare_start,
                         submit_end - submit_start);
  static uint64_t previous = UINT64_MAX;
  if (!profiler.active && previous != revision) {
    previous = revision;
    platform_log(false, "DrawDoTasks",
                 "revision=%llu list=%llu rows=%llu counter=%llu detail=%llu mounted=%zu filter=%s "
                 "items=%zu",
                 (unsigned long long)revision, (unsigned long long)tasks.list_builds,
                 (unsigned long long)tasks.row_builds, (unsigned long long)tasks.counter_builds,
                 (unsigned long long)tasks.detail_builds, tasks.row_count, app_cstr(&tasks.filter),
                 tasks_count(&tasks));
  }
}
static void document_frame(void) {
  bool recording = document_ready && document.config.start_ms &&
                   app_wall_ms() >= document.config.start_ms - 5000 && !document.done;
  document_frames_begin(&document_probe, recording);
  poll_editor();
  if (document_ready && !document_pump(&document))
    platform_log(true, "DocumentSync", "%s", document.error);
  document_frames_work(&document_probe, document_ready ? document.peer_tick : 0);
  layout();
  geometry_count = 0;
  label_slot = 0;
  draw_clip = (ui_rect){0, 0, ui_width, ui_height};
  labels_clip(0, 0, ui_width, sapp_heightf() / scale);
  ui_surface c = surface();
  if (document_ready) {
    document_screen_draw(&document_view, &c);
    if (interaction.text.active && interaction.text.context == &document_view)
      editor_layout(&interaction.text, editor_pixels(document_view.edit_bounds));
  } else {
    c.text("Shared document", 18, 25, 25, ink, 280);
    c.text("Pair both phones with tools/sync_phones.py", 18, 110, 14, muted, 364);
  }
  quad((ui_rect){302, 22, 80, 30}, 0x20323fff, false);
  prepare(90, "Particles", 11, 0);
  label(90, 311, 30, mint);
  surface_access(2, (ui_rect){302, 22, 80, 30}, "Particles", "", ACCESS_BUTTON);
  present();
  document_frames_end(&document_probe);
  if (document_ready)
    clock_probe_pump(&document.calibration);
  if (document_ready && document.done && !document_saved && ++document_drain >= 15) {
    document_saved = document_save(&document, document_frames_json, &document_probe);
    if (document_saved)
      platform_log(false, "DocumentSync", "Complete: edits=%llu",
                   (unsigned long long)(document.local_tick + document.peer_tick));
  }
}
static void particle_frame(void) {
  layout();
  draw_clip = (ui_rect){0, 0, ui_width, ui_height};
  labels_clip(0, 0, ui_width, sapp_heightf() / scale);
  int64_t now = app_now();
  double dt = (now - previous_frame) / 1e9;
  previous_frame = now;
  elapsed += dt;
  frames++;
  display_frames++;
  if (!particles_step(&particles)) {
    error(particles.db.error);
    return;
  }
  particle_sample last = particles.last;
  if (elapsed - last_display > .5 || frames == 1) {
    if (elapsed > last_display) {
      fps = display_frames / (elapsed - last_display);
      display_frames = 0;
    }
    if (particles.paused)
      snprintf(metrics, sizeof(metrics), "Paused    %.2f ms last step", last.total / 1000.0);
    else
      snprintf(metrics, sizeof(metrics), "%.0f fps    %.2f ms step", fps, last.total / 1000.0);
    if (particles.paused)
      snprintf(workload, sizeof(workload), "%d particles   /   96 cell views", particles.count);
    else
      snprintf(workload, sizeof(workload), "%d moving   /   %d cells updated", last.moved,
               last.changed);
    last_display = elapsed;
  }
  prepare(0, "Cell particles", 28, 0);
  prepare(1, "C99  /  Sokol  /  Datafriend", 12, 0);
  for (size_t i = 0; i < 7; i++)
    prepare((int)i + 2, particles.buttons[i].label, i < 4 ? 14 : 13, 0);
  prepare(9, metrics, 14, 0);
  prepare(10, workload, 12, 0);
  prepare(11, particles.status, 12, 0);
  prepare(12, "PARTICLES", 10, 0);
  geometry_count = 0;
  for (size_t i = 0; i < 7; i++)
    quad(button_bounds(i), particles.buttons[i].selected ? 0x28584fff : 0x20323fff, false);
  quad(board, 0x14232eff, false);
  float cw = board.w / 8, ch = board.h / 12;
  for (int i = 0; i < 96; i++)
    if (particles.cells[i].active)
      quad((ui_rect){board.x + (i % 8) * cw, board.y + (i / 8) * ch, cw, ch}, 0x193c3bff, false);
  for (int x = 0; x <= 8; x++)
    quad((ui_rect){board.x + x * cw, board.y, .45f, board.h}, 0x29424cff, false);
  for (int y = 0; y <= 12; y++)
    quad((ui_rect){board.x, board.y + y * ch, board.w, .45f}, 0x29424cff, false);
  for (int i = 0; i < 96; i++) {
    particle_cell *c = &particles.cells[i];
    for (size_t j = 0; j < c->count; j++) {
      particle p = c->items[j];
      float x = board.x + (float)(p.x / 640) * board.w, y = board.y + (float)(p.y / 960) * board.h;
      quad((ui_rect){x - 1.8f, y - 1.8f, 3.6f, 3.6f}, c->active ? 0x8ef5caff : 0x84a7bfff, true);
    }
  }
  label(0, 18, 23, ink);
  label(1, 19, 62, muted);
  label(12, 19, 92, muted);
  for (size_t i = 0; i < 7; i++) {
    ui_rect r = button_bounds(i);
    label((int)i + 2,
          r.x + (i < 4    ? 20
                 : i == 6 ? 29
                          : 15),
          r.y + (i < 4 ? 7 : 8), particles.buttons[i].selected ? mint : ink);
  }
  label(9, 18, board.y + board.h + 16, ink);
  label(10, 18, board.y + board.h + 39, muted);
  label(11, 18, board.y + board.h + 61, mint);
  quad((ui_rect){302, 22, 80, 30}, 0x20323fff, false);
  prepare(90, "Tasks", 12, 0);
  label(90, 320, 29, mint);
  surface_access(3, (ui_rect){302, 22, 80, 30}, "Tasks", "", ACCESS_BUTTON);
  for (size_t i = 0; i < 7; i++)
    surface_access(200 + i, button_bounds(i), particles.buttons[i].label, "",
                   ACCESS_BUTTON | (particles.buttons[i].selected ? ACCESS_SELECTED : 0));
  present();
}
static void event(const sapp_event *);
static void poll_access(void) {
  access_event action;
  while (access_poll(&action)) {
    if (interaction.text.active || profiler.active)
      continue;
    const access_node *node = access_find(&interaction.access, action.id);
    if (!node || (node->flags & ACCESS_DISABLED))
      continue;
    if (action.id == 250 && tasks_mode && action.action != ACCESS_ACTIVATE) {
      task_view.scroll = (float)clamp(
          task_view.scroll + (action.action == ACCESS_FORWARD ? 1 : -1) * task_view.list.h * .8f, 0,
          task_view.max_scroll);
      haptic_play(HAPTIC_SELECTION);
    } else if (action.action == ACCESS_ACTIVATE &&
               (node->flags & (ACCESS_BUTTON | ACCESS_CHECKBOX))) {
      sapp_event tap = {0};
      tap.num_touches = 1;
      tap.touches[0].pos_x = node->bounds.x + node->bounds.w / 2;
      tap.touches[0].pos_y = node->bounds.y + node->bounds.h / 2;
      tap.type = SAPP_EVENTTYPE_TOUCHES_BEGAN;
      event(&tap);
      tap.type = SAPP_EVENTTYPE_TOUCHES_ENDED;
      event(&tap);
      haptic_play(HAPTIC_SELECTION);
    }
    // Rebuild before accepting another action, so stale nodes cannot cross screens.
    break;
  }
}
static void frame(void) {
  if (failed)
    return;
  poll_access();
  access_begin(&interaction.access);
  if (document_mode)
    document_frame();
  else {
    if (document_ready)
      document_pump(&document);
    if (tasks_mode)
      task_frame();
    else
      particle_frame();
  }
  if (!access_end(&interaction.access))
    platform_log(true, "Accessibility", "Snapshot update failed");
}
static void patch(float x, float y) {
  if (!particles_patch(&particles, (int)((x - board.x) / board.w * 8),
                       (int)((y - board.y) / board.h * 12)))
    error(particles.db.error);
}
static void event(const sapp_event *e) {
  if (failed || !tasks_ready || profiler.active)
    return;
  if (e->type == SAPP_EVENTTYPE_SUSPENDED) {
    poll_editor();
    editor_cancel(&interaction.text);
    task_screen_up(&task_view, 0, 0, true);
    if (particles_ready && !particles_validate(&particles))
      error("Particle validation failed");
    return;
  }
  if (e->type == SAPP_EVENTTYPE_RESUMED) {
    previous_frame = app_now();
    elapsed = last_display = 0;
    display_frames = 0;
    return;
  }
  if (!e->num_touches)
    return;
  float x = e->touches[0].pos_x / scale, y = e->touches[0].pos_y / scale - ui_top;
  if (e->type == SAPP_EVENTTYPE_TOUCHES_BEGAN && ui_contains((ui_rect){302, 22, 80, 30}, x, y)) {
    editor_cancel(&interaction.text);
    if (tasks_mode) {
      tasks_mode = false;
      document_mode = true;
    } else if (document_mode) {
      document_mode = false;
      if (!particles_ready) {
        particles_ready = particles_open(&particles, app_cstr(&directory));
        if (!particles_ready)
          error(particles.db.error);
      }
    } else
      tasks_mode = true;
    return;
  }
  if (document_mode) {
    if (document_ready && e->type == SAPP_EVENTTYPE_TOUCHES_BEGAN)
      document_screen_press(&document_view, x, y);
    return;
  }
  if (tasks_mode) {
    if (e->type == SAPP_EVENTTYPE_TOUCHES_BEGAN)
      task_screen_down(&task_view, x, y);
    if (e->type == SAPP_EVENTTYPE_TOUCHES_MOVED)
      task_screen_move(&task_view, x, y);
    if (e->type == SAPP_EVENTTYPE_TOUCHES_ENDED || e->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED)
      task_screen_up(&task_view, x, y, e->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED);
    return;
  }
  if (e->type == SAPP_EVENTTYPE_TOUCHES_ENDED || e->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED) {
    interaction.dragging = false;
    return;
  }
  if (e->type == SAPP_EVENTTYPE_TOUCHES_BEGAN) {
    for (size_t i = 0; i < 7; i++)
      if (ui_contains(button_bounds(i), x, y)) {
        int64_t start = app_us();
        uint64_t before = particles_button_builds(&particles);
        if (!particles_press(&particles, (int)i))
          error(particles.db.error);
        platform_log(
            false, "DrawDoSokol",
            "Action: button=%d edit_us=%lld rebuilt_buttons=%llu count=%d paused=%d local=%d",
            (int)i, (long long)(app_us() - start),
            (unsigned long long)(particles_button_builds(&particles) - before), particles.count,
            particles.paused, particles.local);
        return;
      }
    interaction.dragging = particles.local && ui_contains(board, x, y);
    if (interaction.dragging)
      patch(x, y);
  } else if (e->type == SAPP_EVENTTYPE_TOUCHES_MOVED && interaction.dragging)
    patch(x, y);
}
static void cleanup(void) {
  editor_free(&interaction.text);
  access_free(&interaction.access);
  document_screen_free(&document_view);
  if (document_started)
    document_close(&document);
  if (identity)
    df_identity_close(identity);
  task_screen_free(&task_view);
  if (tasks.db.engine)
    tasks_close(&tasks);
  if (particles.db.engine)
    particles_close(&particles);
  profile_free(&profiler);
  document_frames_free(&document_probe);
  app_buffer_free(&directory);
  if (text_ready)
    labels_shutdown();
  if (gpu_ready) {
    sg_destroy_pipeline(pipeline);
    sg_destroy_shader(shader);
    sg_destroy_buffer(buffer);
    sg_shutdown();
  }
}
sapp_desc sokol_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  sapp_desc d = {0};
  d.init_cb = init;
  d.frame_cb = frame;
  d.event_cb = event;
  d.cleanup_cb = cleanup;
  d.high_dpi = true;
  d.window_title = "Datafriend demo";
  d.logger.func = slog_func;
  return d;
}
