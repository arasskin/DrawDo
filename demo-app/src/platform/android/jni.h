#ifndef APP_ANDROID_JNI_H
#define APP_ANDROID_JNI_H
#include "base.h"
#include "sokol/sokol_app.h"
#include <android/native_activity.h>
typedef struct android_env {
  JavaVM *vm;
  JNIEnv *e;
  bool attached, frame;
} android_env;
static inline bool android_checked(JNIEnv *e) {
  if ((*e)->ExceptionCheck(e)) {
    (*e)->ExceptionDescribe(e);
    (*e)->ExceptionClear(e);
    return false;
  }
  return true;
}
static inline void android_release(android_env *j) {
  if (j->frame)
    (*j->e)->PopLocalFrame(j->e, NULL);
  if (j->attached)
    (*j->vm)->DetachCurrentThread(j->vm);
}
static inline bool android_acquire(android_env *j) {
  memset(j, 0, sizeof(*j));
  ANativeActivity *a = (ANativeActivity *)sapp_android_get_native_activity();
  if (!a)
    return false;
  j->vm = a->vm;
  if ((*j->vm)->GetEnv(j->vm, (void **)&j->e, JNI_VERSION_1_6) != JNI_OK) {
    if ((*j->vm)->AttachCurrentThread(j->vm, &j->e, NULL) != JNI_OK)
      return false;
    j->attached = true;
  }
  if ((*j->e)->PushLocalFrame(j->e, 16) != JNI_OK) {
    android_checked(j->e);
    android_release(j);
    return false;
  }
  j->frame = true;
  return true;
}
#endif
