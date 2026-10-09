#include <stdio.h>
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_log.h"
#include "sokol/sokol_glue.h"
#include "font/text.h"
#include "app/todo_app.h"
#include "db/db_smoke.h"

static struct {
    sg_pass_action pass_action;
} state;

static void init(void) {
    sg_setup({.environment = sglue_environment(),
              .logger.func = slog_func,});

    state.pass_action = {.colors[0] = {.load_action = SG_LOADACTION_CLEAR,
                                       .clear_value = { 0.93f, 0.94f, 0.97f, 1.0f }}};

    text_init();

    // validation suite (temporary); remove once CI-ish coverage exists elsewhere
    fprintf(stderr, "db smoke: %s\n", db_smoke_run());

    todo_app_init();
}

static void frame(void) {
    sg_begin_pass({ .action = state.pass_action, .swapchain = sglue_swapchain() });
    todo_app_frame();
    text_flush();
    sg_end_pass();
    sg_commit();
}

static float to_logical(float v) {
    return v / sapp_dpi_scale();
}

static void event(const sapp_event* e) {
    if (e->type == SAPP_EVENTTYPE_TOUCHES_BEGAN) {
        todo_app_touch(to_logical(e->touches[0].pos_x),
                       to_logical(e->touches[0].pos_y));
    }
}

static void cleanup(void) {
    todo_app_shutdown();
    text_shutdown();
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
    return sapp_desc{.init_cb = init,
                     .frame_cb = frame,
                     .cleanup_cb = cleanup,
                     .event_cb = event,
                     .width = 800,
                     .height = 600,
                     .high_dpi = true,
                     .window_title = "Jun's Todos",
                     .logger.func = slog_func,};
}
