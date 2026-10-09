#include "platform/runtime.h"
#include "sokol/sokol_app.h"
#include <android/native_activity.h>
#include <android/log.h>
#include <atomic>
#include <cstdarg>
#include <stdexcept>
namespace {std::atomic<uint64_t> safe_insets{0};}
extern "C" JNIEXPORT void JNICALL Java_dev_drawdo_sokol_MainActivity_setInsets(JNIEnv*,jclass,jint top,jint bottom){
  safe_insets.store((uint64_t(uint32_t(top))<<32)|uint32_t(bottom),std::memory_order_relaxed);
}
namespace platform {
void keep_awake(bool){} // Activity already requests FLAG_KEEP_SCREEN_ON.
insets safe_area(){auto bits=safe_insets.load(std::memory_order_relaxed);return {float(bits>>32),float(uint32_t(bits))};}
std::string data_directory(){auto* a=(ANativeActivity*)sapp_android_get_native_activity();if(!a||!a->internalDataPath)throw std::runtime_error("Missing application data directory");return a->internalDataPath;}
void log(bool error,const char* tag,const char* format,...){va_list args;va_start(args,format);__android_log_vprint(error?ANDROID_LOG_ERROR:ANDROID_LOG_INFO,tag,format,args);va_end(args);}
bool profile_options(int& count,int& kind,int& warmup,int& frames){
  auto* activity=(ANativeActivity*)sapp_android_get_native_activity();
  JNIEnv* e=nullptr;bool attached=activity->vm->GetEnv((void**)&e,JNI_VERSION_1_6)!=JNI_OK;
  if(attached&&activity->vm->AttachCurrentThread(&e,nullptr)!=JNI_OK)throw std::runtime_error("Profile JNI attach");
  struct detach {JavaVM* vm;bool attached;~detach(){if(attached)vm->DetachCurrentThread();}} guard{activity->vm,attached};
  if(e->PushLocalFrame(8)!=JNI_OK)throw std::runtime_error("Profile JNI local frame");
  struct pop {JNIEnv* e;~pop(){e->PopLocalFrame(nullptr);}} local{e};
  auto cls=e->GetObjectClass(activity->clazz);auto method=e->GetMethodID(cls,"taskProfile","()[I");
  auto values=(jintArray)e->CallObjectMethod(activity->clazz,method);
  if(e->ExceptionCheck()){e->ExceptionDescribe();e->ExceptionClear();throw std::runtime_error("Profile configuration");}
  if(!values||e->GetArrayLength(values)!=4)return false;
  jint v[4];e->GetIntArrayRegion(values,0,4,v);count=v[0];kind=v[1];warmup=v[2];frames=v[3];return true;
}
}
