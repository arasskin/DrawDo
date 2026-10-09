#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
namespace labels {
inline constexpr size_t texture_budget_bytes=100'000'000; // 100 MB, on demand
// Sokol pools have a 16-bit handle ceiling; reserve headroom for other images.
inline constexpr int texture_handle_pool_size=65535;
inline constexpr size_t texture_handle_limit=texture_handle_pool_size-16;
void init();
// Prepare a frame-local drawing slot; equal raster content shares a texture.
// Slots must be prepared before drawing and are invalidated by flush().
void prepare(int slot,const std::string& text,float logical_size,float scale,float max_width=0);
void clip(float x,float y,float width,float height);
void draw(int slot,float x,float y,uint32_t rgba);
void flush(float width,float height);
void shutdown();
}
