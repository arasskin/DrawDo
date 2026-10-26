#include <jni.h>
#include "text_buffers/trace.h"
JNIEXPORT void JNICALL Java_dev_drawdo_text_1latency_MainActivity_trace(JNIEnv *env, jobject self, jint stage, jlong ns) {
  (void)env; (void)self;
  text_trace_record(999999, (unsigned)stage, (uint64_t)ns);
}
