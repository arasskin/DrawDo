#include "clipboard/clipboard.h"
#include "text_buffers/text.h"
#include "native/android.h"
static native_peer peer;
JNIEXPORT void JNICALL Java_dev_drawdo_clipboard_Clipboard_install(JNIEnv *e, jobject self) {
  native_install(e, &peer, self);
}
bool clipboard_set(const char *text) {
  if (!text || !text_valid(text, strlen(text)))
    return false;
  native_env j;
  if (!native_acquire(&peer, &j))
    return false;
  jmethodID method = native_method(&j, &peer, "set", "([B)V");
  jbyteArray bytes = native_utf8(j.e, text);
  bool ok = native_checked(j.e) && method && bytes;
  if (ok) {
    (*j.e)->CallVoidMethod(j.e, peer.object, method, bytes);
    ok = native_checked(j.e);
  }
  native_release(&j);
  return ok;
}
bool clipboard_get(ct_bytes *out) {
  native_env j;
  if (!native_acquire(&peer, &j))
    return false;
  jmethodID method = native_method(&j, &peer, "get", "()[B");
  jbyteArray bytes = method ? (*j.e)->CallObjectMethod(j.e, peer.object, method) : NULL;
  bool ok = native_checked(j.e) && bytes;
  ct_bytes next = {0};
  if (ok) {
    jsize n = (*j.e)->GetArrayLength(j.e, bytes);
    ok = ct_array_reserve(&next.data, &next.capacity, (size_t)n + 1, 1);
    if (ok) {
      (*j.e)->GetByteArrayRegion(j.e, bytes, 0, n, (jbyte *)next.data);
      next.data[n] = 0;
      next.size = (size_t)n;
      ok = native_checked(j.e) && text_valid(ct_bytes_string(&next), next.size);
    }
  }
  if (ok) {
    ct_bytes old = *out;
    *out = next;
    next = old;
  }
  ct_bytes_free(&next);
  native_release(&j);
  return ok;
}
