#include "profiling/presentation.h"
#ifdef __ANDROID__
#include <EGL/egl.h>
#include <EGL/eglext.h>
static EGLDisplay display;
static EGLSurface surface;
static PFNEGLGETNEXTFRAMEIDANDROIDPROC next_frame;
static PFNEGLGETFRAMETIMESTAMPSANDROIDPROC timestamps;
#endif
bool presentation_init(void) {
#ifdef __ANDROID__
  display = eglGetCurrentDisplay();
  surface = eglGetCurrentSurface(EGL_DRAW);
  const char *ext = eglQueryString(display, EGL_EXTENSIONS);
  if (!ext || !strstr(ext, "EGL_ANDROID_get_frame_timestamps"))
    return false;
  next_frame = (PFNEGLGETNEXTFRAMEIDANDROIDPROC)eglGetProcAddress("eglGetNextFrameIdANDROID");
  timestamps =
      (PFNEGLGETFRAMETIMESTAMPSANDROIDPROC)eglGetProcAddress("eglGetFrameTimestampsANDROID");
  PFNEGLGETFRAMETIMESTAMPSUPPORTEDANDROIDPROC supports =
      (PFNEGLGETFRAMETIMESTAMPSUPPORTEDANDROIDPROC)eglGetProcAddress(
          "eglGetFrameTimestampSupportedANDROID");
  return next_frame && timestamps && supports &&
         supports(display, surface, EGL_DISPLAY_PRESENT_TIME_ANDROID) &&
         supports(display, surface, EGL_RENDERING_COMPLETE_TIME_ANDROID) &&
         supports(display, surface, EGL_COMPOSITION_LATCH_TIME_ANDROID) &&
         supports(display, surface, EGL_REQUESTED_PRESENT_TIME_ANDROID) &&
         eglSurfaceAttrib(display, surface, EGL_TIMESTAMPS_ANDROID, EGL_TRUE);
#else
  return false;
#endif
}
void presentation_begin(presentation_stamp *s) {
  *s = (presentation_stamp){0, -1, -1, -1, -1, NULL};
#ifdef __ANDROID__
  if (next_frame)
    next_frame(display, surface, &s->id);
#endif
}
void presentation_track(presentation_stamp *s, const void *raw) {
  (void)s;
  (void)raw;
}
void presentation_collect(presentation_stamp *s) {
#ifdef __ANDROID__
  if (timestamps && s->id && s->present < 0) {
    EGLint fields[] = {EGL_REQUESTED_PRESENT_TIME_ANDROID, EGL_RENDERING_COMPLETE_TIME_ANDROID,
                       EGL_COMPOSITION_LATCH_TIME_ANDROID, EGL_DISPLAY_PRESENT_TIME_ANDROID};
    EGLnsecsANDROID values[4];
    if (timestamps(display, surface, s->id, 4, fields, values)) {
      s->queue = values[0];
      s->gpu_done = values[1];
      s->latch = values[2];
      s->present = values[3];
    }
  }
#else
  (void)s;
#endif
}
void presentation_release(presentation_stamp *s) { (void)s; }
