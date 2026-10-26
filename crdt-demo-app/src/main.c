#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_log.h"
#include "app.h"
#include "platform.h"
#include "loop.h"
#include "text_buffers/native.h"
#ifdef CRDT_DEMO_TIMING
#include "timing.h"
#endif
static app state;
static bool ready;
static bool foreground = true;
static void process(void) {
  if (!ready || !foreground)
    return;
  bool step = true;
#ifdef CRDT_DEMO_TIMING
  text_trace_record(999998, 50, text_trace_clock());
  step = timing_tick(&state);
#endif
  if (step && !app_step(&state, platform_bounds())) {
    ready = false;
    platform_error(state.error);
  }
#if CRDT_DEMO_EVENTS
  df_network_watch watch = {-1, 0, UINT64_MAX};
  if (ready && step)
    watch = df_network_prepare(state.document.network);
#ifdef CRDT_DEMO_TIMING
  uint64_t timeout = timing_timeout();
  if (ready && timeout < watch.timeout_us)
    watch.timeout_us = timeout;
#endif
  if (!loop_watch(watch)) {
    ready = false;
    platform_error("Cannot watch application events");
  }
#endif
}
static void init(void) {
  sg_setup(&(sg_desc){.environment = sglue_environment(), .logger.func = slog_func});
  const char *directory = platform_init();
#if CRDT_DEMO_EVENTS
  if (!loop_open(process)) {
    platform_error("Cannot open application event loop");
    return;
  }
  editor_native_notify(loop_wake);
#endif
#ifdef CRDT_DEMO_TIMING
  directory = timing_directory(directory);
#endif
  ready = directory && app_open(&state, directory, platform_bounds(), 18 * platform_scale());
#ifdef CRDT_DEMO_TIMING
  if (ready)
    timing_start(directory);
#endif
  if (!ready)
    platform_error(state.error[0] ? state.error : "Cannot initialize application");
  process();
}
static void frame(void) {
#ifdef CRDT_DEMO_TIMING
  text_trace_record(999998, 10, text_trace_clock());
#endif
#if !CRDT_DEMO_EVENTS
  process();
#endif
  sg_begin_pass(&(sg_pass){
      .action.colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {1, 1, 1, 1}},
      .swapchain = sglue_swapchain()});
  sg_end_pass();
  sg_commit();
}
static void event(const sapp_event *event) {
#ifdef CRDT_DEMO_TIMING
  if (event->type == SAPP_EVENTTYPE_SUSPENDED || event->type == SAPP_EVENTTYPE_RESUMED) {
    text_trace_record(999998, event->type == SAPP_EVENTTYPE_SUSPENDED ? 60 : 61,
                      text_trace_clock());
    timing_save();
  }
#endif
  if (ready && event->type == SAPP_EVENTTYPE_SUSPENDED) {
    app_pause(&state);
    foreground = false;
#if CRDT_DEMO_EVENTS
    loop_watch((df_network_watch){-1, 0, UINT64_MAX});
#endif
  }
  if (event->type == SAPP_EVENTTYPE_RESUMED) {
    foreground = true;
    process();
  }
  if (event->type == SAPP_EVENTTYPE_RESIZED)
    process();
}
static void cleanup(void) {
#if CRDT_DEMO_EVENTS
  editor_native_notify(NULL);
  loop_close();
#endif
#ifdef CRDT_DEMO_TIMING
  timing_save();
#endif
  app_close(&state);
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
                     .window_title = "CRDT demo",
                     .high_dpi = true,
                     .width = 400,
                     .height = 800,
                     .logger.func = slog_func};
}
