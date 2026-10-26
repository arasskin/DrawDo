#include "present.h"
#include "sokol/sokol_app.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <string.h>
static PFNEGLGETNEXTFRAMEIDANDROIDPROC next_frame;
static PFNEGLGETFRAMETIMESTAMPSANDROIDPROC timestamps;
static EGLDisplay display;
static EGLSurface surface;
static bool supported;
static void configure(void) {
  EGLDisplay d = eglGetCurrentDisplay();
  EGLSurface s = eglGetCurrentSurface(EGL_DRAW);
  if (d == display && s == surface)
    return;
  display = d;
  surface = s;
  supported = false;
  const char *extensions = eglQueryString(d, EGL_EXTENSIONS);
  if (!extensions || !strstr(extensions, "EGL_ANDROID_get_frame_timestamps"))
    return;
  next_frame = (PFNEGLGETNEXTFRAMEIDANDROIDPROC)eglGetProcAddress("eglGetNextFrameIdANDROID");
  timestamps =
      (PFNEGLGETFRAMETIMESTAMPSANDROIDPROC)eglGetProcAddress("eglGetFrameTimestampsANDROID");
  PFNEGLGETFRAMETIMESTAMPSUPPORTEDANDROIDPROC supports =
      (PFNEGLGETFRAMETIMESTAMPSUPPORTEDANDROIDPROC)eglGetProcAddress(
          "eglGetFrameTimestampSupportedANDROID");
  supported = next_frame && timestamps && supports &&
              supports(d, s, EGL_DISPLAY_PRESENT_TIME_ANDROID) &&
              eglSurfaceAttrib(d, s, EGL_TIMESTAMPS_ANDROID, EGL_TRUE);
}
void present_track(presentation *p, const void *drawable) {
  (void)drawable;
  if (p->armed)
    return;
  configure();
  EGLuint64KHR id;
  p->fallback = !supported || !next_frame(display, surface, &id);
  p->frame = p->fallback ? sapp_frame_count() : id;
  p->armed = true;
}
bool present_ready(presentation *p) {
  if (!p->armed)
    return false;
  if (p->fallback) {
    /* Drivers without presentation timestamps: let the submitted swap pass
     * through the swapchain, then complete GPU work before the UI-thread close.
     * This is a conservative queue-drain fallback, not a display timestamp. */
    if (sapp_frame_count() - p->frame < 3)
      return false;
    glFinish();
    __android_log_print(ANDROID_LOG_DEBUG, "DrawDoHandoff", "release after swapchain fallback");
    return true;
  }
  EGLint field = EGL_DISPLAY_PRESENT_TIME_ANDROID;
  EGLnsecsANDROID time = EGL_TIMESTAMP_PENDING_ANDROID;
  if (!timestamps(display, surface, p->frame, 1, &field, &time)) {
    supported = false;
    p->armed = false;
    return false;
  }
  if (time == EGL_TIMESTAMP_INVALID_ANDROID) {
    /* A dropped frame cannot acknowledge presentation. Track a later one. */
    p->armed = false;
    return false;
  }
  if (time <= 0)
    return false;
  __android_log_print(ANDROID_LOG_DEBUG, "DrawDoHandoff",
                      "release after frame %llu presented at %lld", (unsigned long long)p->frame,
                      (long long)time);
  return true;
}
void present_reset(presentation *p) { *p = (presentation){0}; }
