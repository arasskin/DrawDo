#include "screen.h"
#include "render/text.h"
#include "sokol/sokol_gfx.h"
#include "render/shaders.h"
typedef struct vertex {
  float x, y, u, v;
  uint32_t color;
  float circle;
} vertex;
static vertex vertices[8192];
static size_t count;
static bool upload;
static sg_buffer buffer;
static sg_shader shader;
static sg_pipeline pipeline;
static uint32_t packed(uint32_t c) {
  uint32_t a = c & 255;
  return ((((c >> 24) & 255) * a + 127) / 255) | (((((c >> 16) & 255) * a + 127) / 255) << 8) |
         (((((c >> 8) & 255) * a + 127) / 255) << 16) | (a << 24);
}
static void quad(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4,
                 uint32_t color) {
  uint32_t c = packed(color);
  vertex v[] = {{x1, y1, 0, 0, c, 0}, {x2, y2, 0, 0, c, 0}, {x3, y3, 0, 0, c, 0},
                {x1, y1, 0, 0, c, 0}, {x3, y3, 0, 0, c, 0}, {x4, y4, 0, 0, c, 0}};
  if (count + COUNT(v) <= COUNT(vertices)) {
    memcpy(vertices + count, v, sizeof(v));
    count += COUNT(v);
  }
}
static void rect(float x, float y, float w, float h, uint32_t c) {
  quad(x, y, x + w, y, x + w, y + h, x, y + h, c);
}
static void line(float x, float y, float x2, float y2, float stroke, uint32_t c) {
  float length = hypotf(x2 - x, y2 - y), dx = (y2 - y) / length * stroke / 2,
        dy = (x - x2) / length * stroke / 2;
  quad(x + dx, y + dy, x2 + dx, y2 + dy, x2 - dx, y2 - dy, x - dx, y - dy, c);
}
bool screen_open(void) {
  buffer = sg_make_buffer(&(sg_buffer_desc){
      .size = sizeof(vertices), .usage = {.vertex_buffer = true, .dynamic_update = true}});
  shader = sg_make_shader(shape_shader_desc(sg_query_backend()));
  sg_pipeline_desc p = {0};
  p.shader = shader;
  p.layout.attrs[ATTR_shape_pos].format = SG_VERTEXFORMAT_FLOAT2;
  p.layout.attrs[ATTR_shape_uv0].format = SG_VERTEXFORMAT_FLOAT2;
  p.layout.attrs[ATTR_shape_color0].format = SG_VERTEXFORMAT_UBYTE4N;
  p.layout.attrs[ATTR_shape_circle0].format = SG_VERTEXFORMAT_FLOAT;
  p.colors[0].blend.enabled = true;
  p.colors[0].blend.src_factor_rgb = p.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
  p.colors[0].blend.dst_factor_rgb = p.colors[0].blend.dst_factor_alpha =
      SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  pipeline = sg_make_pipeline(&p);
  return sg_query_pipeline_state(pipeline) == SG_RESOURCESTATE_VALID && labels_init();
}
bool screen_build(todo_screen *screen, todo_model *m, editor_rect bounds, float scale,
                  bool editing) {
  screen->bounds = bounds;
  screen->scale = scale;
  screen->count = 0;
  count = 0;
  upload = true;
  labels_begin();
  access_begin(&screen->accessibility);
  todo_style *s = &m->style;
  float x = bounds.x / scale + s->margin_left, y = bounds.y / scale + s->margin_top;
  float bottom = (bounds.y + bounds.h) / scale - s->margin_bottom;
  float hit = maximum(s->hit_size, s->checkbox_size);
  float width = minimum(s->text_width,
                        (bounds.x + bounds.w) / scale - s->margin_right - x - hit - s->text_gap);
  float row_min = maximum(s->row_height, hit);
  size_t limit = (size_t)clamp((bottom - y) / (row_min + s->row_gap) + 1, 0, LABEL_CAPACITY - 1);
  if (!todo_mount(m, limit))
    return false;
  labels_clip(bounds.x / scale, bounds.y / scale, bounds.w / scale, bounds.h / scale);
  size_t mounted = 0;
  for (size_t i = 0; i <= m->ids.count && i < LABEL_CAPACITY && y + row_min <= bottom && width > 0;
       ++i) {
    bool add = i == m->ids.count;
    if (add && !m->writable)
      break;
    uint32_t id = add ? 0 : m->ids.data[i];
    todo_row *r = add ? NULL : todo_find(m, id);
    if (!add && (!r || !r->exists))
      break;
    const char *text = add ? ct_bytes_string(&s->add_label) : ct_bytes_string(&r->title);
    float text_y = y + s->text_inset;
    if (text_y >= bottom)
      break;
    if (!labels_prepare_paragraph((int)i, text, s->font_size, scale, width, bottom - text_y))
      return false;
    float height = maximum(row_min, s->text_inset + labels_height((int)i));
    screen_item item = {id,
                        {x, y, hit, height},
                        {x + hit + s->text_gap, text_y, width,
                         maximum(s->font_size * 1.3f, labels_height((int)i))}};
    screen->items[screen->count++] = item;
    if (!add)
      ++mounted;
    uint32_t color = add ? s->add_color : s->checkbox_color;
    float box = s->checkbox_size, bx = x + (hit - box) / 2, by = y + (hit - box) / 2;
    float stroke = minimum(s->checkbox_stroke, box / 2);
    if (r && r->done) {
      rect(bx, by, box, box, color);
      line(bx + box * .20f, by + box * .50f, bx + box * .43f, by + box * .73f, s->check_stroke,
           s->check_color);
      line(bx + box * .43f, by + box * .73f, bx + box * .82f, by + box * .27f, s->check_stroke,
           s->check_color);
    } else {
      rect(bx, by, box, stroke, color);
      rect(bx, by + box - stroke, box, stroke, color);
      rect(bx, by + stroke, stroke, box - 2 * stroke, color);
      rect(bx + box - stroke, by + stroke, stroke, box - 2 * stroke, color);
    }
    /* Keep the cached text under the opaque native overlay while its async open
     * is pending, avoiding a blank frame during the handoff. */
    labels_draw((int)i, item.text.x, item.text.y,
                add ? s->add_color : (r->done ? s->done_color : s->text_color));
    if (!editing) {
      access_rect box_bounds = {item.row.x * scale, item.row.y * scale, item.row.w * scale,
                                item.row.h * scale};
      access_rect text_bounds = {item.text.x * scale, item.text.y * scale, item.text.w * scale,
                                 item.text.h * scale};
      unsigned disabled = m->writable ? 0 : ACCESS_DISABLED;
      if (add) {
        box_bounds.w = (hit + s->text_gap + width) * scale;
        if (!access_add(&screen->accessibility, 1, box_bounds, "New task", "", ACCESS_BUTTON))
          return false;
      } else {
        if (!access_add(&screen->accessibility, (uint64_t)id * 2 + 2, box_bounds, text, "",
                        ACCESS_CHECKBOX | (r->done ? ACCESS_CHECKED : 0) | disabled) ||
            !access_add(&screen->accessibility, (uint64_t)id * 2 + 3, text_bounds, text,
                        "Edit task", ACCESS_BUTTON | disabled))
          return false;
      }
    }
    y += height + s->row_gap;
  }
  if (!todo_mount(m, mounted) || !access_end(&screen->accessibility))
    return false;
  m->dirty = false;
  return true;
}
void screen_draw(float width, float height) {
  if (count) {
    if (upload)
      sg_update_buffer(buffer, &(sg_range){vertices, count * sizeof(*vertices)});
    sg_apply_pipeline(pipeline);
    shape_params_t params = {.fb_size = {width, height}};
    sg_apply_uniforms(UB_shape_params, &SG_RANGE(params));
    sg_apply_bindings(&(sg_bindings){.vertex_buffers[0] = buffer});
    sg_draw(0, (int)count, 1);
  }
  upload = false;
  labels_flush(width, height);
}
const screen_item *screen_hit(todo_screen *s, float x, float y, bool *checkbox) {
  x /= s->scale;
  y /= s->scale;
  for (size_t i = 0; i < s->count; ++i) {
    const screen_item *item = s->items + i;
    if (y >= item->row.y && y < item->row.y + item->row.h && x >= item->row.x &&
        x < item->text.x + item->text.w) {
      *checkbox = item->id && x < item->row.x + item->row.w;
      return item;
    }
  }
  return NULL;
}
void screen_close(todo_screen *s) {
  access_free(&s->accessibility);
  labels_shutdown();
  sg_destroy_pipeline(pipeline);
  sg_destroy_shader(shader);
  sg_destroy_buffer(buffer);
}
