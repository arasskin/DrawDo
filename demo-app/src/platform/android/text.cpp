// Android text shaping/rasterization; GPU caching lives in render/text.cpp.
#include "platform/text_raster.h"
#include "profiling/metrics.h"
#include "sokol/sokol_app.h"
#include <android/native_activity.h>
#include <android/bitmap.h>
#include <cstring>
#include <stdexcept>
namespace text_raster {
namespace {
JavaVM* vm=nullptr;jclass activity_class=nullptr;jmethodID raster=nullptr;
struct environment {
  JNIEnv* e=nullptr;bool attached=false;
  environment(){task_profile::timer t(task_profile::current.jni_attach);if(vm->GetEnv(reinterpret_cast<void**>(&e),JNI_VERSION_1_6)!=JNI_OK){if(vm->AttachCurrentThread(&e,nullptr)!=JNI_OK)throw std::runtime_error("JNI attach failed");attached=true;if(task_profile::enabled)++task_profile::current.jni_attachments;}}
  ~environment(){task_profile::timer t(task_profile::current.jni_detach);if(attached)vm->DetachCurrentThread();}
};
void checked(JNIEnv* e){if(e->ExceptionCheck()){e->ExceptionDescribe();e->ExceptionClear();throw std::runtime_error("Android text service failed");}}
}
void init(){
  auto* activity=(ANativeActivity*)sapp_android_get_native_activity();vm=activity->vm;
  environment env;auto* e=env.e;
  auto cls=e->GetObjectClass(activity->clazz);checked(e);
  activity_class=(jclass)e->NewGlobalRef(cls);e->DeleteLocalRef(cls);
  raster=e->GetStaticMethodID(activity_class,"rasterText","([BFI)Landroid/graphics/Bitmap;");checked(e);
  if(!raster)throw std::runtime_error("Missing text adapter");
}
bitmap render(const std::string& text,float pixels,int width){
    environment env;auto* e=env.e;
    if(e->PushLocalFrame(8)!=JNI_OK)throw std::runtime_error("JNI local frame failed");
    struct pop{JNIEnv* e;~pop(){e->PopLocalFrame(nullptr);}} guard{e};
    auto bytes=e->NewByteArray(jsize(text.size()));checked(e);
    e->SetByteArrayRegion(bytes,0,jsize(text.size()),reinterpret_cast<const jbyte*>(text.data()));checked(e);
    jobject bitmap;
    {task_profile::timer t(task_profile::current.java_raster);bitmap=e->CallStaticObjectMethod(activity_class,raster,bytes,pixels,jint(width));checked(e);}
    if(!bitmap)throw std::runtime_error("No text bitmap");
    auto copy_start=task_profile::enabled?task_profile::now():0;
    AndroidBitmapInfo info{};if(AndroidBitmap_getInfo(e,bitmap,&info)!=0||info.format!=ANDROID_BITMAP_FORMAT_RGBA_8888)throw std::runtime_error("Unsupported text bitmap");
    void* data=nullptr;if(AndroidBitmap_lockPixels(e,bitmap,&data)!=0)throw std::runtime_error("Cannot read text bitmap");
    text_raster::bitmap result{int(info.width),int(info.height),std::vector<uint8_t>(size_t(info.width)*info.height*4)};
    auto& copy=result.pixels;
    for(uint32_t row=0;row<info.height;row++)std::memcpy(copy.data()+size_t(row)*info.width*4,static_cast<uint8_t*>(data)+size_t(row)*info.stride,info.width*4);
    AndroidBitmap_unlockPixels(e,bitmap);
    if(task_profile::enabled)task_profile::current.bitmap_copy+=task_profile::now()-copy_start;
    return result;
}
void shutdown(){
  if(activity_class){environment env;env.e->DeleteGlobalRef(activity_class);activity_class=nullptr;}
}
}
