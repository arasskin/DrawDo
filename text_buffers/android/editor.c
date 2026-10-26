#include "text_buffers/native.h"
#include "native/android.h"
static native_peer peer;
JNIEXPORT void JNICALL Java_dev_drawdo_textbuffers_TextEditor_install(JNIEnv *e, jobject self) {
  native_install(e, &peer, self);
}
#include <pthread.h>
static pthread_mutex_t mailbox_lock = PTHREAD_MUTEX_INITIALIZER;
static editor_snapshot mailbox;
static bool present;
static void (*notify)(void);
void editor_native_notify(void (*wake)(void)) {
  pthread_mutex_lock(&mailbox_lock);
  notify = wake;
  pthread_mutex_unlock(&mailbox_lock);
}
JNIEXPORT void JNICALL Java_dev_drawdo_textbuffers_TextEditor_finishEditor(
    JNIEnv *e, jclass cls, jlong id, jlong revision, jbyteArray bytes, jint anchor, jint caret,
    jint start, jint end, jint events) {
  (void)cls;
  editor_snapshot next = {0};
  jsize n = (*e)->GetArrayLength(e, bytes);
  if (n < 0 || !ct_array_reserve(&next.text.data, &next.text.capacity, (size_t)n + 1, 1))
    return;
  (*e)->GetByteArrayRegion(e, bytes, 0, n, (jbyte *)next.text.data);
  if (!native_checked(e)) {
    ct_bytes_free(&next.text);
    return;
  }
  next.text.data[n] = 0;
  next.text.size = (size_t)n;
  next.id = (uint64_t)id;
  next.revision = (uint64_t)revision;
  next.anchor = anchor;
  next.caret = caret;
  next.marked_start = start;
  next.marked_end = end;
  next.events = (unsigned)events;
  pthread_mutex_lock(&mailbox_lock);
  if (present && mailbox.id == next.id)
    next.events |= mailbox.events;
  ct_bytes_free(&mailbox.text);
  mailbox = next;
  present = true;
  if (notify)
    notify();
  pthread_mutex_unlock(&mailbox_lock);
}
bool platform_editor_poll(editor_snapshot *out) {
  pthread_mutex_lock(&mailbox_lock);
  bool found = present;
  if (found) {
    *out = mailbox;
    memset(&mailbox, 0, sizeof(mailbox));
    present = false;
  }
  pthread_mutex_unlock(&mailbox_lock);
  return found;
}
static jbyteArray utf8(JNIEnv *e, const char *text) {
  size_t n = strlen(text);
  if (n > INT32_MAX)
    return NULL;
  jbyteArray bytes = (*e)->NewByteArray(e, (jsize)n);
  if (bytes)
    (*e)->SetByteArrayRegion(e, bytes, 0, (jsize)n, (const jbyte *)text);
  return bytes;
}
bool platform_editor_open(uint64_t id, const char *text, editor_options o) {
  native_env j;
  if (!native_acquire(&peer, &j))
    return false;
  JNIEnv *e = j.e;
  jclass cls = (*e)->GetObjectClass(e, peer.object);
  jmethodID method = cls ? (*e)->GetMethodID(e, cls, "openEditor", "(J[BIIIIFIIZZ[BZFFZZ)V") : NULL;
  jbyteArray bytes = utf8(e, text), label = utf8(e, o.label ? o.label : "Text");
  bool ok = native_checked(e) && method && bytes && label;
  if (ok) {
    (*e)->CallVoidMethod(e, peer.object, method, (jlong)id, bytes, (jint)o.bounds.x,
                         (jint)o.bounds.y, (jint)o.bounds.w, (jint)o.bounds.h,
                         (jfloat)o.font_pixels, (jint)o.foreground, (jint)o.background,
                         (jboolean)o.multiline, (jboolean)o.live, label, (jboolean)o.place_at_point,
                         (jfloat)o.point_x, (jfloat)o.point_y, (jboolean)o.embedded,
                         (jboolean)o.retain_on_submit);
    ok = native_checked(e);
  }
  native_release(&j);
  return ok;
}
bool platform_editor_set(uint64_t id, uint64_t expected, const char *text, size_t anchor,
                         size_t caret) {
  if (anchor > INT32_MAX || caret > INT32_MAX)
    return false;
  native_env j;
  if (!native_acquire(&peer, &j))
    return false;
  JNIEnv *e = j.e;
  jclass cls = (*e)->GetObjectClass(e, peer.object);
  jmethodID method = cls ? (*e)->GetMethodID(e, cls, "updateEditor", "(JJ[BII)V") : NULL;
  jbyteArray bytes = utf8(e, text);
  bool ok = native_checked(e) && method && bytes;
  if (ok) {
    (*e)->CallVoidMethod(e, peer.object, method, (jlong)id, (jlong)expected, bytes, (jint)anchor,
                         (jint)caret);
    ok = native_checked(e);
  }
  native_release(&j);
  return ok;
}
bool platform_editor_layout(uint64_t id, editor_rect r) {
  native_env j;
  if (!native_acquire(&peer, &j))
    return false;
  JNIEnv *e = j.e;
  jclass cls = (*e)->GetObjectClass(e, peer.object);
  jmethodID method = cls ? (*e)->GetMethodID(e, cls, "layoutEditor", "(JIIII)V") : NULL;
  bool ok = native_checked(e) && method;
  if (ok) {
    (*e)->CallVoidMethod(e, peer.object, method, (jlong)id, (jint)r.x, (jint)r.y, (jint)r.w,
                         (jint)r.h);
    ok = native_checked(e);
  }
  native_release(&j);
  return ok;
}
void platform_editor_close(uint64_t id) {
  native_env j;
  if (!native_acquire(&peer, &j))
    return;
  JNIEnv *e = j.e;
  jclass cls = (*e)->GetObjectClass(e, peer.object);
  jmethodID method = cls ? (*e)->GetMethodID(e, cls, "cancelEditor", "(J)V") : NULL;
  if (native_checked(e) && method) {
    (*e)->CallVoidMethod(e, peer.object, method, (jlong)id);
    native_checked(e);
  }
  native_release(&j);
}

#ifdef TEXT_BUFFERS_TRACE
#include "text_buffers/trace.h"
#endif
JNIEXPORT jboolean JNICALL Java_dev_drawdo_textbuffers_TextEditor_timingEnabled(JNIEnv *e,
                                                                                jclass c) {
  (void)e;
  (void)c;
#ifdef TEXT_BUFFERS_TRACE
  return JNI_TRUE;
#else
  return JNI_FALSE;
#endif
}
JNIEXPORT void JNICALL Java_dev_drawdo_textbuffers_TextEditor_trace(JNIEnv *e, jclass c, jint tag,
                                                                    jint stage, jlong ns) {
  (void)e;
  (void)c;
#ifdef TEXT_BUFFERS_TRACE
  text_trace_record((uint32_t)tag, (unsigned)stage, (uint64_t)ns);
#else
  (void)tag;
  (void)stage;
  (void)ns;
#endif
}
