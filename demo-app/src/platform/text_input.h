#pragma once
#include <cstdint>
#include <functional>
#include <string>
namespace editor {
void open(std::string text,float x,float y,float width,float height,std::function<void(std::string)> submit);
void open_live(std::string text,float x,float y,float width,float height,std::function<void(std::string)> changed);
void poll();
void cancel();
}
