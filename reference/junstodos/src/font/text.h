#pragma once
#include <stdint.h>

// Portable text renderer: system-font glyphs cached in a texture atlas,
// drawn as textured quads through sokol_gfx. The platform-specific work
// (shaping, font fallback, rasterization) lives behind text_platform.h.
//
// Usage per frame, inside the render pass:
//   text_draw(...);  // any number of times
//   text_flush();    // once, before sg_end_pass()
//
// Coordinates are logical pixels (same units as sapp touch events), origin
// top-left; x,y in text_draw is the top-left corner of the line box.
// Colors are 0xRRGGBBAA.

void  text_init(void);      // call after sg_setup()
void  text_shutdown(void);  // call before sg_shutdown()

void  text_draw(const char* utf8, float x, float y, float px_size, uint32_t rgba);
float text_measure(const char* utf8, float px_size);  // line width in logical pixels
void  text_flush(void);
