#pragma once
#include <cstdint>
#include <string>
namespace platform {
struct insets {float top,bottom;};
insets safe_area();
std::string data_directory();
void keep_awake(bool);
void log(bool error,const char* tag,const char* format,...);
// False for ordinary interactive launches.
bool profile_options(int& count,int& kind,int& warmup,int& frames);
}
