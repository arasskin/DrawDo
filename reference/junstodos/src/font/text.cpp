// Portable half of the text renderer (see text.h): glyph atlas with shelf
// packing, glyph cache, and a per-frame quad batch drawn through sokol_gfx.
// Everything platform-specific arrives through text_platform.h.

#include "text.h"
#include "text_platform.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "../shaders.h"

#include <unordered_map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ATLAS_SIZE = 1024,
    MAX_QUADS = 4096,
};

typedef struct {
    float x, y;
    float u, v;
    uint32_t color;  // UBYTE4N, premultiplied
} text_vertex;

typedef struct {
    bool visible;    // has pixels in the atlas
    bool is_color;
    uint16_t ax, ay, aw, ah;   // atlas rect, device pixels
    float offset_x, offset_y;  // bitmap top-left relative to glyph pen
} cached_glyph;

static struct {
    sg_image atlas_img;
    sg_view atlas_view;
    sg_sampler smp;
    sg_buffer vbuf;
    sg_pipeline pip;
    uint8_t* atlas;            // CPU copy, RGBA8
    bool atlas_dirty;
    int shelf_x, shelf_y, shelf_h;
    bool atlas_full_warned;
    std::unordered_map<uint64_t, cached_glyph>* cache;
    text_vertex* verts;
    int num_verts;
} state;

void text_init(void) {
    state.atlas = (uint8_t*)calloc(1, ATLAS_SIZE * ATLAS_SIZE * 4);
    state.verts = (text_vertex*)malloc(sizeof(text_vertex) * MAX_QUADS * 6);
    state.cache = new std::unordered_map<uint64_t, cached_glyph>();
    state.shelf_x = state.shelf_y = state.shelf_h = 0;

    state.atlas_img = sg_make_image({.usage = {.dynamic_update = true},
                                     .width = ATLAS_SIZE,
                                     .height = ATLAS_SIZE,
                                     .pixel_format = SG_PIXELFORMAT_RGBA8,
                                     .label = "text-atlas"});
    state.atlas_view = sg_make_view({.texture = {.image = state.atlas_img},
                                     .label = "text-atlas-view"});
    state.smp = sg_make_sampler({.min_filter = SG_FILTER_LINEAR,
                                 .mag_filter = SG_FILTER_LINEAR,
                                 .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
                                 .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
                                 .label = "text-sampler"});
    state.vbuf = sg_make_buffer({.size = sizeof(text_vertex) * MAX_QUADS * 6,
                                 .usage = {.vertex_buffer = true, .stream_update = true},
                                 .label = "text-vertices"});
    state.pip = sg_make_pipeline({
        .shader = sg_make_shader(text_shader_desc(sg_query_backend())),
        .layout = {.attrs = {[ATTR_text_pos].format = SG_VERTEXFORMAT_FLOAT2,
                             [ATTR_text_texcoord0].format = SG_VERTEXFORMAT_FLOAT2,
                             [ATTR_text_color0].format = SG_VERTEXFORMAT_UBYTE4N}},
        .colors = {[0].blend = {.enabled = true,
                                .src_factor_rgb = SG_BLENDFACTOR_ONE,
                                .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                .src_factor_alpha = SG_BLENDFACTOR_ONE,
                                .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA}},
        .label = "text-pipeline"});
}

void text_shutdown(void) {
    sg_destroy_pipeline(state.pip);
    sg_destroy_buffer(state.vbuf);
    sg_destroy_sampler(state.smp);
    sg_destroy_view(state.atlas_view);
    sg_destroy_image(state.atlas_img);
    delete state.cache;
    free(state.verts);
    free(state.atlas);
    memset(&state, 0, sizeof(state));
}

static const cached_glyph* get_glyph(uint32_t font_id, uint32_t glyph_id) {
    uint64_t key = ((uint64_t)font_id << 32) | glyph_id;
    auto it = state.cache->find(key);
    if (it != state.cache->end()) {
        return &it->second;
    }
    cached_glyph g = {};
    txt_bitmap bm;
    if (txt_rasterize(font_id, glyph_id, &bm) && bm.w > 0 && bm.h > 0) {
        if (state.shelf_x + bm.w + 1 > ATLAS_SIZE) {  // start a new shelf
            state.shelf_y += state.shelf_h + 1;
            state.shelf_x = 0;
            state.shelf_h = 0;
        }
        if (state.shelf_y + bm.h + 1 <= ATLAS_SIZE) {
            for (int row = 0; row < bm.h; row++) {
                memcpy(state.atlas + (((size_t)(state.shelf_y + row) * ATLAS_SIZE + (size_t)state.shelf_x) * 4),
                       bm.pixels + (size_t)row * (size_t)bm.w * 4,
                       (size_t)bm.w * 4);
            }
            g.visible = true;
            g.is_color = bm.is_color;
            g.ax = (uint16_t)state.shelf_x;
            g.ay = (uint16_t)state.shelf_y;
            g.aw = (uint16_t)bm.w;
            g.ah = (uint16_t)bm.h;
            g.offset_x = bm.offset_x;
            g.offset_y = bm.offset_y;
            state.shelf_x += bm.w + 1;
            if (bm.h > state.shelf_h) {
                state.shelf_h = bm.h;
            }
            state.atlas_dirty = true;
        } else if (!state.atlas_full_warned) {
            fprintf(stderr, "text: glyph atlas full, some glyphs will not render\n");
            state.atlas_full_warned = true;
        }
        txt_bitmap_free(&bm);
    }
    return &state.cache->emplace(key, g).first->second;
}

// pack premultiplied bytes for a UBYTE4N attribute (byte 0 = R)
static uint32_t pack_premul(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    r = (r * a + 127) / 255;
    g = (g * a + 127) / 255;
    b = (b * a + 127) / 255;
    return r | (g << 8) | (b << 16) | (a << 24);
}

void text_draw(const char* utf8, float x, float y, float px_size, uint32_t rgba) {
    float scale = sapp_dpi_scale();
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    txt_glyph glyphs[TXT_MAX_GLYPHS];
    txt_line_metrics lm;
    int n = txt_shape(utf8, px_size * scale, glyphs, TXT_MAX_GLYPHS, &lm);
    float baseline = y + lm.ascent / scale;

    uint32_t a = rgba & 0xFF;
    uint32_t mono_color = pack_premul((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, a);
    uint32_t emoji_color = pack_premul(255, 255, 255, a);  // fade only, no tint

    for (int i = 0; i < n; i++) {
        const cached_glyph* cg = get_glyph(glyphs[i].font_id, glyphs[i].glyph_id);
        if (!cg->visible) {
            continue;
        }
        if (state.num_verts + 6 > MAX_QUADS * 6) {
            return;
        }
        float x0 = x + (glyphs[i].x + cg->offset_x) / scale;
        float y0 = baseline + (glyphs[i].y + cg->offset_y) / scale;
        float x1 = x0 + cg->aw / scale;
        float y1 = y0 + cg->ah / scale;
        float u0 = cg->ax / (float)ATLAS_SIZE;
        float v0 = cg->ay / (float)ATLAS_SIZE;
        float u1 = (cg->ax + cg->aw) / (float)ATLAS_SIZE;
        float v1 = (cg->ay + cg->ah) / (float)ATLAS_SIZE;
        uint32_t color = cg->is_color ? emoji_color : mono_color;

        text_vertex* v = &state.verts[state.num_verts];
        v[0] = {x0, y0, u0, v0, color};
        v[1] = {x1, y0, u1, v0, color};
        v[2] = {x1, y1, u1, v1, color};
        v[3] = {x0, y0, u0, v0, color};
        v[4] = {x1, y1, u1, v1, color};
        v[5] = {x0, y1, u0, v1, color};
        state.num_verts += 6;
    }
}

float text_measure(const char* utf8, float px_size) {
    float scale = sapp_dpi_scale();
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    txt_glyph glyphs[TXT_MAX_GLYPHS];
    txt_line_metrics lm;
    txt_shape(utf8, px_size * scale, glyphs, TXT_MAX_GLYPHS, &lm);
    return lm.width / scale;
}

void text_flush(void) {
    if (state.atlas_dirty) {
        sg_update_image(state.atlas_img,
                        {.mip_levels = {{.ptr = state.atlas, .size = ATLAS_SIZE * ATLAS_SIZE * 4}}});
        state.atlas_dirty = false;
    }
    if (state.num_verts == 0) {
        return;
    }
    float scale = sapp_dpi_scale();
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    sg_update_buffer(state.vbuf, {.ptr = state.verts,
                                  .size = (size_t)state.num_verts * sizeof(text_vertex)});
    sg_apply_pipeline(state.pip);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = state.vbuf;
    bind.views[VIEW_tex] = state.atlas_view;
    bind.samplers[SMP_smp] = state.smp;
    sg_apply_bindings(&bind);
    text_vs_params_t params = {};
    params.fb_size[0] = sapp_widthf() / scale;
    params.fb_size[1] = sapp_heightf() / scale;
    sg_apply_uniforms(UB_text_vs_params, SG_RANGE(params));
    sg_draw(0, state.num_verts, 1);
    state.num_verts = 0;
}
