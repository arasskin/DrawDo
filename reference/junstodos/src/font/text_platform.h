#pragma once
#include <stdint.h>

// Platform text engine seam. The portable renderer (text.cpp) talks to the
// system font stack exclusively through this interface; porting to a new
// platform means reimplementing these three functions (see text_ios.mm).
//
// All coordinates are device pixels, x right, y down, origin at the pen
// position on the baseline.

#define TXT_MAX_GLYPHS (512)

typedef struct {
    uint32_t font_id;   // interned by the platform layer; fallback fonts get
                        // their own ids, and the size is baked into the id
    uint32_t glyph_id;  // font-specific glyph index (NOT a codepoint)
    float x, y;         // pen-relative baseline position
} txt_glyph;

typedef struct {
    float width;    // typographic advance of the whole line
    float ascent;   // baseline to top
    float descent;  // baseline to bottom (positive)
} txt_line_metrics;

typedef struct {
    uint8_t* pixels;  // RGBA8, premultiplied alpha, row 0 = top
    int w, h;
    float offset_x;   // bitmap top-left relative to the glyph pen position
    float offset_y;   // (normally negative: the glyph sits above the baseline)
    bool is_color;    // color glyph (emoji): already colored, must not be tinted
} txt_bitmap;

// Shape a UTF-8 string with the system font at px_size device pixels,
// including font fallback, bidi and complex-script shaping. Fills out[]
// with up to max positioned glyphs, returns the count.
int txt_shape(const char* utf8, float px_size, txt_glyph* out, int max,
              txt_line_metrics* lm);

// Rasterize one glyph. Returns false on failure; true with w == 0 or h == 0
// means the glyph has no visible pixels (e.g. a space).
bool txt_rasterize(uint32_t font_id, uint32_t glyph_id, txt_bitmap* out);

void txt_bitmap_free(txt_bitmap* bm);
