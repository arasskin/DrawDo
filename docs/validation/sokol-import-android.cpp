// Compile/link probe only; not an installed or device-tested application.
#define SOKOL_IMPL
#define SOKOL_GLES3
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_log.h"
#include "drawdo-sokol-shaders.h"
static void init() { sg_desc d{}; d.environment=sglue_environment(); d.logger.func=slog_func; sg_setup(&d); sg_make_shader(simple_shader_desc(sg_query_backend())); sg_make_shader(text_shader_desc(sg_query_backend())); }
static void frame() { sg_pass p{};p.swapchain=sglue_swapchain();sg_begin_pass(&p);sg_end_pass();sg_commit(); }
static void cleanup() { sg_shutdown(); }
sapp_desc sokol_main(int, char**) { sapp_desc d{};d.init_cb=init;d.frame_cb=frame;d.cleanup_cb=cleanup;d.logger.func=slog_func;return d; }
