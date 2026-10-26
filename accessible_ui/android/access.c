#include "accessible_ui/native.h"
#include "native/android.h"
static native_peer peer;
JNIEXPORT void JNICALL Java_dev_drawdo_accessibleui_Accessibility_install(JNIEnv *e, jobject self) {
  native_install(e, &peer, self);
}
JNIEXPORT jboolean JNICALL Java_dev_drawdo_accessibleui_Accessibility_activate(JNIEnv *e,
                                                                               jclass cls,
                                                                               jlong key,
                                                                               jint action) {
  (void)e;
  (void)cls;
  return platform_accessibility_send((uint64_t)key, (unsigned)action);
}
bool platform_accessibility_set(const access_snapshot *s) {
  if (s->count > INT32_MAX / 4)
    return false;
  native_env j;
  if (!native_acquire(&peer, &j))
    return false;
  JNIEnv *e = j.e;
  jsize count = (jsize)s->count;
  jmethodID method = native_method(&j, &peer, "update", "([J[F[I[[B[[B)V");
  jlongArray keys = (*e)->NewLongArray(e, count);
  jfloatArray rects = (*e)->NewFloatArray(e, count * 4);
  jintArray flags = (*e)->NewIntArray(e, count);
  jclass bytes = (*e)->FindClass(e, "[B");
  jobjectArray labels = bytes ? (*e)->NewObjectArray(e, count, bytes, NULL) : NULL;
  jobjectArray values = bytes ? (*e)->NewObjectArray(e, count, bytes, NULL) : NULL;
  bool ok = native_checked(e) && method && keys && rects && flags && labels && values;
  for (jsize i = 0; ok && i < count; i++) {
    const access_node *n = &s->nodes[i];
    jlong key = (jlong)n->id;
    jint f = (jint)n->flags;
    jfloat r[] = {n->bounds.x, n->bounds.y, n->bounds.w, n->bounds.h};
    (*e)->SetLongArrayRegion(e, keys, i, 1, &key);
    (*e)->SetIntArrayRegion(e, flags, i, 1, &f);
    (*e)->SetFloatArrayRegion(e, rects, i * 4, 4, r);
    jbyteArray label = native_utf8(e, (const char *)s->strings.data + n->label);
    jbyteArray value = native_utf8(e, (const char *)s->strings.data + n->value);
    ok = native_checked(e) && label && value;
    if (ok) {
      (*e)->SetObjectArrayElement(e, labels, i, label);
      (*e)->SetObjectArrayElement(e, values, i, value);
      ok = native_checked(e);
    }
    if (label)
      (*e)->DeleteLocalRef(e, label);
    if (value)
      (*e)->DeleteLocalRef(e, value);
  }
  if (ok) {
    (*e)->CallVoidMethod(e, peer.object, method, keys, rects, flags, labels, values);
    ok = native_checked(e);
  }
  native_release(&j);
  return ok;
}
