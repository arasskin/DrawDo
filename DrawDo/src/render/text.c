#include "render/text.h"
#include "render/text_cache.h"
#include "render/raster.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "render/shaders.h"
typedef struct text_vertex {
  float x, y, u, v;
  uint32_t color;
} text_vertex;
typedef struct prepared_label {
  cache_resource *texture;
  float scale;
} prepared_label;
typedef struct label_draw {
  cache_resource *texture;
  float x, y, w, h;
} label_draw;
static text_cache cache;
static prepared_label slots[LABEL_CAPACITY];
static text_vertex vertices[LABEL_CAPACITY * 6];
static label_draw draws[LABEL_CAPACITY];
static size_t draw_count;
static bool upload;
static float clip_x, clip_y, clip_w = 10000, clip_h = 10000;
static sg_sampler sampler;
static sg_buffer buffer;
static sg_pipeline pipeline;
static sg_shader shader;
void labels_begin(void) {
  draw_count = 0;
  cache_end_frame(&cache);
  memset(slots, 0, sizeof(slots));
  upload = true;
}
float labels_height(int slot) {
  return slots[slot].texture ? slots[slot].texture->height / slots[slot].scale : 0;
}
static void release(cache_resource *r) {
  if (r->view)
    sg_destroy_view((sg_view){r->view});
  if (r->image)
    sg_destroy_image((sg_image){r->image});
}
static uint32_t packed(uint32_t c) {
  uint32_t a = c & 255;
  return ((((c >> 24) & 255) * a + 127) / 255) | (((((c >> 16) & 255) * a + 127) / 255) << 8) |
         (((((c >> 8) & 255) * a + 127) / 255) << 16) | (a << 24);
}
bool labels_init(void) {
  cache_init(&cache, 8u << 20, LABEL_HANDLE_LIMIT, release);
  if (!text_raster_init())
    return false;
  sg_sampler_desc sm = {0};
  sm.min_filter = sm.mag_filter = SG_FILTER_LINEAR;
  sm.wrap_u = sm.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
  sampler = sg_make_sampler(&sm);
  sg_buffer_desc bd = {0};
  bd.size = sizeof(vertices);
  bd.usage.vertex_buffer = bd.usage.stream_update = true;
  buffer = sg_make_buffer(&bd);
  sg_pipeline_desc pd = {0};
  shader = sg_make_shader(text_shader_desc(sg_query_backend()));
  pd.shader = shader;
  pd.layout.attrs[ATTR_text_pos].format = SG_VERTEXFORMAT_FLOAT2;
  pd.layout.attrs[ATTR_text_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
  pd.layout.attrs[ATTR_text_color0].format = SG_VERTEXFORMAT_UBYTE4N;
  pd.colors[0].blend.enabled = true;
  pd.colors[0].blend.src_factor_rgb = pd.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
  pd.colors[0].blend.dst_factor_rgb = pd.colors[0].blend.dst_factor_alpha =
      SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  pipeline = sg_make_pipeline(&pd);
  return sg_query_pipeline_state(pipeline) == SG_RESOURCESTATE_VALID;
}
static bool build(void *ctx, cache_resource *r) {
  text_bitmap *b = ctx;
  r->width = b->width;
  r->height = b->height;
  sg_image_desc image = {0};
  image.width = b->width;
  image.height = b->height;
  image.pixel_format = SG_PIXELFORMAT_RGBA8;
  image.data.mip_levels[0] = (sg_range){b->pixels, b->size};
  sg_image im = sg_make_image(&image);
  r->image = im.id;
  if (sg_query_image_state(im) != SG_RESOURCESTATE_VALID)
    return false;
  sg_view_desc desc = {0};
  desc.texture.image = im;
  sg_view view = sg_make_view(&desc);
  r->view = view.id;
  return sg_query_view_state(view) == SG_RESOURCESTATE_VALID;
}
static bool prepare_text(int slot, const char *text, float size, float scale, float max_width,
                         float max_height) {
  if (slot < 0 || slot >= LABEL_CAPACITY || scale <= 0)
    return false;
  float pixels = size * scale;
  int width = (int)(max_width * scale);
  int height = (int)(max_height * scale);
  cache_resource *r = cache_find(&cache, text, pixels, width, height);
  if (!r) {
    text_bitmap b;
    if (!text_raster_render(text, pixels, width, height, &b))
      return false;
    r = cache_insert(&cache, text, pixels, width, height, b.size, build, &b);
    free(b.pixels);
    if (!r)
      return false;
  }
  slots[slot] = (prepared_label){r, scale};
  return true;
}
bool labels_prepare(int slot, const char *text, float size, float scale, float width) {
  return prepare_text(slot, text, size, scale, width, 0);
}
bool labels_prepare_paragraph(int slot, const char *text, float size, float scale, float width,
                              float height) {
  return width > 0 && height > 0 && prepare_text(slot, text, size, scale, width, height);
}
void labels_clip(float x, float y, float w, float h) {
  clip_x = x;
  clip_y = y;
  clip_w = w;
  clip_h = h;
}
bool labels_draw(int slot, float x, float y, uint32_t rgba) {
  if (slot < 0 || slot >= LABEL_CAPACITY || !slots[slot].texture || draw_count >= LABEL_CAPACITY)
    return false;
  prepared_label p = slots[slot];
  float right = x + p.texture->width / p.scale, bottom = y + p.texture->height / p.scale;
  uint32_t c = packed(rgba);
  text_vertex v[] = {{x, y, 0, 0, c}, {right, y, 1, 0, c},      {right, bottom, 1, 1, c},
                     {x, y, 0, 0, c}, {right, bottom, 1, 1, c}, {x, bottom, 0, 1, c}};
  memcpy(vertices + draw_count * 6, v, sizeof(v));
  draws[draw_count++] = (label_draw){p.texture, clip_x, clip_y, clip_w, clip_h};
  return true;
}
void labels_flush(float width, float height) {
  if (draw_count) {
    sg_range data = {vertices, draw_count * 6 * sizeof(text_vertex)};
    if (upload)
      sg_update_buffer(buffer, &data);
    sg_apply_pipeline(pipeline);
    text_vs_params_t params = {0};
    params.fb_size[0] = width;
    params.fb_size[1] = height;
    sg_apply_uniforms(UB_text_vs_params, &SG_RANGE(params));
    for (size_t i = 0; i < draw_count; i++) {
      label_draw d = draws[i];
      float scale = sapp_widthf() / width;
      sg_apply_scissor_rect((int)(d.x * scale), (int)(d.y * scale), (int)(d.w * scale),
                            (int)(d.h * scale), true);
      sg_bindings bind = {0};
      bind.vertex_buffers[0] = buffer;
      bind.views[VIEW_tex] = (sg_view){d.texture->view};
      bind.samplers[SMP_smp] = sampler;
      sg_apply_bindings(&bind);
      sg_draw((int)i * 6, 6, 1);
    }
  }
  upload = false;
}
void labels_shutdown(void) {
  cache_clear(&cache);
  memset(slots, 0, sizeof(slots));
  draw_count = 0;
  sg_destroy_pipeline(pipeline);
  sg_destroy_shader(shader);
  sg_destroy_buffer(buffer);
  sg_destroy_sampler(sampler);
  text_raster_shutdown();
}
