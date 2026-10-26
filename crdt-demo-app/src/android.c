#include "platform.h"
#include "loop.h"
#include "sokol/sokol_app.h"
#include <android/native_activity.h>
#include <android/configuration.h>
#include <android/window.h>
#include <android/log.h>
#include <pthread.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static editor_rect visible;
static float density = 1;
JNIEXPORT void JNICALL Java_dev_drawdo_crdt_MainActivity_setBounds(JNIEnv *e, jclass cls, jint x,
                                                                   jint y, jint w, jint h) {
  (void)e;
  (void)cls;
  pthread_mutex_lock(&lock);
  visible = (editor_rect){x, y, w, h};
  pthread_mutex_unlock(&lock);
  loop_wake();
}
const char *platform_init(void) {
  ANativeActivity *activity = (ANativeActivity *)sapp_android_get_native_activity();
  if (!activity)
    return NULL;
#ifdef CRDT_DEMO_TIMING
  ANativeActivity_setWindowFlags(activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
#endif
  AConfiguration *configuration = AConfiguration_new();
  AConfiguration_fromAssetManager(configuration, activity->assetManager);
  int dpi = AConfiguration_getDensity(configuration);
  AConfiguration_delete(configuration);
  if (dpi > 0 && dpi < 10000)
    density = dpi / 160.0f;
  return activity->internalDataPath;
}
float platform_scale(void) { return density; }
editor_rect platform_bounds(void) {
  pthread_mutex_lock(&lock);
  editor_rect r = visible;
  pthread_mutex_unlock(&lock);
  float margin = 16 * density;
  if (r.w <= 0 || r.h <= 0)
    r = (editor_rect){0, 0, sapp_widthf(), sapp_heightf()};
  r.x += margin;
  r.y += margin;
  r.w -= 2 * margin;
  r.h -= 2 * margin;
  if (r.h < 1)
    r.h = 1;
  return r;
}
void platform_error(const char *message) {
  __android_log_print(ANDROID_LOG_ERROR, "CRDTdemo", "%s", message);
  ANativeActivity *activity = (ANativeActivity *)sapp_android_get_native_activity();
  JNIEnv *env = NULL;
  bool attached =
      (*activity->vm)->GetEnv(activity->vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED;
  if (attached && (*activity->vm)->AttachCurrentThread(activity->vm, &env, NULL) != JNI_OK)
    return;
  jclass cls = (*env)->GetObjectClass(env, activity->clazz);
  jmethodID method = (*env)->GetMethodID(env, cls, "showError", "(Ljava/lang/String;)V");
  jstring text = (*env)->NewStringUTF(env, message);
  if (method && text)
    (*env)->CallVoidMethod(env, activity->clazz, method, text);
  (*env)->DeleteLocalRef(env, text);
  (*env)->DeleteLocalRef(env, cls);
  if (attached)
    (*activity->vm)->DetachCurrentThread(activity->vm);
}
void platform_shutdown(void) {}
