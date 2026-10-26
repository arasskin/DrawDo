#include <jni.h>
#include <dlfcn.h>
#include "clipboard/clipboard.h"
#include "haptics/haptics.h"
JNIEXPORT jstring JNICALL Java_dev_drawdo_tests_ServicesVerification_nativeChecks(JNIEnv *e, jclass cls) {
  (void)cls;
  void *lib=dlopen("libdrawdo_sokol.so",RTLD_NOW);
  if(!lib) return (*e)->NewStringUTF(e,"App library unavailable");
  bool (*set)(const char *)=dlsym(lib,"clipboard_set");
  bool (*get)(ct_bytes *)=dlsym(lib,"clipboard_get");
  void (*play)(enum haptic_kind)=dlsym(lib,"haptic_play");
  const char *failure=NULL;
  ct_bytes out={0};
  const char *sample="Clipboard 🌱 café 日本語 e\xcc\x81\nsecond line";
  if(!set || !get || !play) failure="Missing C exports";
  else if(!set(sample) || !get(&out) || strcmp(ct_bytes_string(&out),sample)) failure="UTF-8 clipboard round trip";
  else if(set("\xff")) failure="Invalid UTF-8 accepted";
  else if(!get(&out) || strcmp(ct_bytes_string(&out),sample)) failure="Invalid write changed clipboard";
  else if(!set("") || !get(&out) || out.size) failure="Empty clipboard round trip";
  if(!failure) { play(HAPTIC_SELECTION); play(HAPTIC_IMPACT); play(HAPTIC_SUCCESS); }
  ct_bytes_free(&out); dlclose(lib);
  return failure ? (*e)->NewStringUTF(e,failure) : NULL;
}
