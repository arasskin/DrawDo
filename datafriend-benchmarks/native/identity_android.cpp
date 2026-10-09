#ifdef __ANDROID__
#include "identity.h"
#include <jni.h>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {
JavaVM* vm = nullptr;
jclass store_class = nullptr;
jobject app_context = nullptr;
jmethodID load_method = nullptr;
std::mutex setup_mutex;
void check(JNIEnv* env) {
  if(env->ExceptionCheck()) {
    // Never format Java exceptions: provider messages may contain sensitive data.
    env->ExceptionClear();
    throw std::runtime_error("Datafriend Android secure identity storage failed");
  }
}
struct environment {
  JNIEnv* env = nullptr;
  bool attached = false;
  environment() {
    if(vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
      if(vm->AttachCurrentThread(&env, nullptr) != JNI_OK)
        throw std::runtime_error("cannot attach identity storage thread");
      attached = true;
    }
    if(!env) throw std::runtime_error("identity JNI unavailable");
    if(env->PushLocalFrame(8) != JNI_OK) {
      env->ExceptionClear();
      if(attached) vm->DetachCurrentThread();
      throw std::bad_alloc{};
    }
  }
  ~environment() { env->PopLocalFrame(nullptr); if(attached) vm->DetachCurrentThread(); }
};
}
extern "C" JNIEXPORT void JNICALL
Java_dev_drawdo_datafriend_IdentityStore_initialize(JNIEnv* env, jclass cls, jobject context) {
  std::lock_guard lock(setup_mutex);
  if(app_context) return;
  env->GetJavaVM(&vm);
  auto type = static_cast<jclass>(env->NewGlobalRef(cls));
  auto app = env->NewGlobalRef(context);
  auto method = env->GetStaticMethodID(cls, "loadOrCreate",
    "(Landroid/content/Context;Ljava/lang/String;[B)[B");
  if(!type || !app || !method || env->ExceptionCheck()) {
    if(type) env->DeleteGlobalRef(type);
    if(app) env->DeleteGlobalRef(app);
    return; // pending exception is delivered to install(), never into Dart.
  }
  store_class = type; app_context = app; load_method = method;
}
namespace datafriend {
void identity_load_or_create(std::string_view alias, uint8_t seed[32]) {
  std::lock_guard lock(setup_mutex);
  if(!app_context) throw std::runtime_error("Datafriend Android identity storage is not initialized");
  environment scope;
  auto* env = scope.env;
  auto name = env->NewStringUTF(std::string(alias).c_str()); check(env);
  auto candidate = env->NewByteArray(32); check(env);
  env->SetByteArrayRegion(candidate, 0, 32, reinterpret_cast<const jbyte*>(seed)); check(env);
  auto loaded = static_cast<jbyteArray>(env->CallStaticObjectMethod(
    store_class, load_method, app_context, name, candidate));
  // Java clears candidate in its finally block, even on provider/storage failure.
  check(env);
  if(!loaded || env->GetArrayLength(loaded) != 32)
    throw std::runtime_error("invalid Android identity");
  env->GetByteArrayRegion(loaded, 0, 32, reinterpret_cast<jbyte*>(seed));
  jbyte zero[32]{};
  env->SetByteArrayRegion(loaded, 0, 32, zero); check(env);
}
}
#endif
