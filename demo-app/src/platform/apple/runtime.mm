#include "platform/runtime.h"
#include "sokol/sokol_app.h"
#import <UIKit/UIKit.h>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
namespace platform {
void keep_awake(bool enabled){UIApplication.sharedApplication.idleTimerDisabled=enabled;}
insets safe_area(){UIWindow* window=(__bridge UIWindow*)sapp_ios_get_window();auto area=window.safeAreaInsets;float scale=window.screen.scale;return {float(area.top)*scale,float(area.bottom)*scale};}
std::string data_directory(){return NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject.UTF8String;}
void log(bool error,const char* tag,const char* format,...){va_list args;va_start(args,format);fprintf(stderr,"%s%s: ",error?"ERROR ":"",tag);vfprintf(stderr,format,args);fputc('\n',stderr);va_end(args);}
bool profile_options(int& count,int& kind,int& warmup,int& frames){
  auto args=NSProcessInfo.processInfo.arguments;auto index=[args indexOfObject:@"--task-profile"];
  if(index==NSNotFound)return false;
  // Automated actions are not physical input; keep the display awake while profiling.
  UIApplication.sharedApplication.idleTimerDisabled=YES;
  if(index+4>=args.count)throw std::runtime_error("Expected count case warmup frames after --task-profile");
  count=std::stoi([args[index+1] UTF8String]);kind=std::stoi([args[index+2] UTF8String]);
  warmup=std::stoi([args[index+3] UTF8String]);frames=std::stoi([args[index+4] UTF8String]);return true;
}
}
