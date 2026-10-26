#include "platform/runtime.h"
#include "platform/android/jni.h"
#include <android/log.h>
#include <pthread.h>
static pthread_mutex_t insets_lock = PTHREAD_MUTEX_INITIALIZER;
static platform_insets insets;
JNIEXPORT void JNICALL Java_dev_drawdo_sokol_MainActivity_setInsets(JNIEnv *e, jclass cls, jint top,
                                                                    jint bottom) {
  (void)e;
  (void)cls;
  pthread_mutex_lock(&insets_lock);
  insets = (platform_insets){(float)top, (float)bottom};
  pthread_mutex_unlock(&insets_lock);
}
void platform_keep_awake(bool enabled) { (void)enabled; }
platform_insets platform_safe_area(void) {
  pthread_mutex_lock(&insets_lock);
  platform_insets value = insets;
  pthread_mutex_unlock(&insets_lock);
  return value;
}
const char *platform_directory(void) {
  ANativeActivity *a = (ANativeActivity *)sapp_android_get_native_activity();
  return a ? a->internalDataPath : NULL;
}
void platform_log(bool error, const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  __android_log_vprint(error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, tag, format, args);
  va_end(args);
}
bool platform_profile_options(int *count, int *kind, int *warmup, int *frames) {
  android_env j;
  if (!android_acquire(&j))
    return false;
  JNIEnv *e = j.e;
  ANativeActivity *a = (ANativeActivity *)sapp_android_get_native_activity();
  jclass cls = (*e)->GetObjectClass(e, a->clazz);
  jmethodID method = cls ? (*e)->GetMethodID(e, cls, "taskProfile", "()[I") : NULL;
  jintArray values = method ? (jintArray)(*e)->CallObjectMethod(e, a->clazz, method) : NULL;
  bool ok = android_checked(e) && values && (*e)->GetArrayLength(e, values) == 4;
  if (ok) {
    jint v[4];
    (*e)->GetIntArrayRegion(e, values, 0, 4, v);
    ok = android_checked(e);
    *count = v[0];
    *kind = v[1];
    *warmup = v[2];
    *frames = v[3];
  }
  android_release(&j);
  return ok;
}

void platform_profile_metadata(FILE *out) { (void)out; }
