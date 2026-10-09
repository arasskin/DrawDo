#include "platform/text_input.h"
#include "sokol/sokol_app.h"
#include <android/native_activity.h>
#include <mutex>
#include <optional>
#include <memory>
#include <stdexcept>
namespace {
struct completion {int64_t token;std::string text;bool cancelled;bool final;};
std::mutex mailbox_mutex;std::optional<completion> mailbox;
int64_t token=0;// Keep the callable itself alive across frame deliveries, including mutable
// captures. Copying std::function here would reset each live editor draft.
std::shared_ptr<std::function<void(std::string)>> callback;
struct env {
  JavaVM* vm;JNIEnv* e=nullptr;bool attached=false;
  env(JavaVM* vm):vm(vm){if(vm->GetEnv((void**)&e,JNI_VERSION_1_6)!=JNI_OK){if(vm->AttachCurrentThread(&e,nullptr)!=JNI_OK)throw std::runtime_error("Editor JNI attach");attached=true;}}
  ~env(){if(attached)vm->DetachCurrentThread();}
};
}
extern "C" JNIEXPORT void JNICALL Java_dev_drawdo_sokol_MainActivity_finishEditor(JNIEnv* e,jclass,jlong id,jbyteArray bytes,jboolean cancelled,jboolean final){
  auto size=e->GetArrayLength(bytes);std::string text(size,'\0');
  e->GetByteArrayRegion(bytes,0,size,(jbyte*)text.data());
  std::lock_guard lock(mailbox_mutex);mailbox=completion{id,std::move(text),bool(cancelled),bool(final)};
}
namespace editor {
void open_impl(std::string text,float x,float y,float width,float height,std::function<void(std::string)> submit,bool live){
  auto* activity=(ANativeActivity*)sapp_android_get_native_activity();env j(activity->vm);
  auto cls=j.e->GetObjectClass(activity->clazz);
  auto method=j.e->GetMethodID(cls,"openEditor","(J[BIIIIZ)V");
  auto bytes=j.e->NewByteArray(jsize(text.size()));j.e->SetByteArrayRegion(bytes,0,jsize(text.size()),(const jbyte*)text.data());
  callback=std::make_shared<std::function<void(std::string)>>(std::move(submit));++token;
  j.e->CallVoidMethod(activity->clazz,method,jlong(token),bytes,jint(x),jint(y),jint(width),jint(height),jboolean(live));
  j.e->DeleteLocalRef(bytes);j.e->DeleteLocalRef(cls);
  if(j.e->ExceptionCheck()){j.e->ExceptionDescribe();j.e->ExceptionClear();callback={};throw std::runtime_error("Editor unavailable");}
}
void open(std::string text,float x,float y,float width,float height,std::function<void(std::string)> submit){open_impl(std::move(text),x,y,width,height,std::move(submit),false);}
void open_live(std::string text,float x,float y,float width,float height,std::function<void(std::string)> submit){open_impl(std::move(text),x,y,width,height,std::move(submit),true);}
void poll(){
  std::optional<completion> message;
  {std::lock_guard lock(mailbox_mutex);message=std::move(mailbox);mailbox.reset();}
  if(message&&message->token==token){auto submit=callback;if(message->final)callback={};if(!message->cancelled&&submit)(*submit)(std::move(message->text));}
}
void cancel(){callback={};++token;}
}
