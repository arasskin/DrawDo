#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace text_raster {
struct bitmap { int width,height; std::vector<uint8_t> pixels; };
void init();
bitmap render(const std::string& text,float pixels,int width);
void shutdown();
}
