#ifndef NATIVE_ANDROID_H
#define NATIVE_ANDROID_H
#include <jni.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef struct native_peer {
  JavaVM *vm;
  jobject object;
} native_peer;
typedef struct native_env {
  JavaVM *vm;
  JNIEnv *e;
  bool attached, frame;
} native_env;
static inline bool native_checked(JNIEnv *e) {
  if (!(*e)->ExceptionCheck(e))
    return true;
  (*e)->ExceptionDescribe(e);
  (*e)->ExceptionClear(e);
  return false;
}
/* Install before starting the app loop; dispose after stopping it. */
static inline void native_install(JNIEnv *e, native_peer *p, jobject object) {
  if (p->object)
    (*e)->DeleteGlobalRef(e, p->object);
  (*e)->GetJavaVM(e, &p->vm);
  p->object = (*e)->NewGlobalRef(e, object);
}
static inline void native_release(native_env *j) {
  if (j->frame)
    (*j->e)->PopLocalFrame(j->e, NULL);
  if (j->attached)
    (*j->vm)->DetachCurrentThread(j->vm);
}
static inline bool native_acquire(native_peer *p, native_env *j) {
  memset(j, 0, sizeof(*j));
  if (!p->object)
    return false;
  j->vm = p->vm;
  if ((*j->vm)->GetEnv(j->vm, (void **)&j->e, JNI_VERSION_1_6) != JNI_OK) {
    if ((*j->vm)->AttachCurrentThread(j->vm, &j->e, NULL) != JNI_OK)
      return false;
    j->attached = true;
  }
  if ((*j->e)->PushLocalFrame(j->e, 16) != JNI_OK) {
    native_checked(j->e);
    native_release(j);
    return false;
  }
  j->frame = true;
  return true;
}
static inline jmethodID native_method(native_env *j, native_peer *p, const char *name,
                                      const char *signature) {
  jclass cls = (*j->e)->GetObjectClass(j->e, p->object);
  return cls ? (*j->e)->GetMethodID(j->e, cls, name, signature) : NULL;
}
static inline jbyteArray native_utf8(JNIEnv *e, const char *s) {
  size_t n = strlen(s);
  if (n > INT32_MAX)
    return NULL;
  jbyteArray a = (*e)->NewByteArray(e, (jsize)n);
  if (a)
    (*e)->SetByteArrayRegion(e, a, 0, (jsize)n, (const jbyte *)s);
  return a;
}
#endif
