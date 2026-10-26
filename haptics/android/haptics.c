#include "haptics/haptics.h"
#include "native/android.h"
static native_peer peer;
JNIEXPORT void JNICALL Java_dev_drawdo_haptics_Haptics_install(JNIEnv *e, jobject self) {
  native_install(e, &peer, self);
}
void haptic_play(enum haptic_kind kind) {
  native_env j;
  if (!native_acquire(&peer, &j))
    return;
  jmethodID method = native_method(&j, &peer, "play", "(I)V");
  if (native_checked(j.e) && method) {
    (*j.e)->CallVoidMethod(j.e, peer.object, method, (jint)kind);
    native_checked(j.e);
  }
  native_release(&j);
}
