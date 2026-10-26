#include "render/raster.h"
#include "android/jni.h"
#include <android/bitmap.h>
static jclass activity_class;
static jmethodID raster;
bool text_raster_init(void) {
  android_env j;
  if (!android_acquire(&j))
    return false;
  JNIEnv *e = j.e;
  ANativeActivity *a = (ANativeActivity *)sapp_android_get_native_activity();
  jclass cls = (*e)->GetObjectClass(e, a->clazz);
  if (cls)
    activity_class = (jclass)(*e)->NewGlobalRef(e, cls);
  if (activity_class)
    raster = (*e)->GetStaticMethodID(e, activity_class, "rasterText",
                                     "([BFII)Landroid/graphics/Bitmap;");
  bool ok = android_checked(e) && raster;
  android_release(&j);
  return ok;
}
bool text_raster_render(const char *text, float pixels, int width, int height, text_bitmap *out) {
  memset(out, 0, sizeof(*out));
  android_env j;
  if (!android_acquire(&j))
    return false;
  JNIEnv *e = j.e;
  bool ok = false;
  size_t n = strlen(text);
  if (n > INT32_MAX)
    goto done;
  jbyteArray bytes = (*e)->NewByteArray(e, (jsize)n);
  if (!bytes || !android_checked(e))
    goto done;
  (*e)->SetByteArrayRegion(e, bytes, 0, (jsize)n, (const jbyte *)text);
  if (!android_checked(e))
    goto done;
  jobject bitmap = (*e)->CallStaticObjectMethod(e, activity_class, raster, bytes, pixels,
                                                (jint)width, (jint)height);
  if (!android_checked(e) || !bitmap)
    goto done;
  AndroidBitmapInfo info;
  if (AndroidBitmap_getInfo(e, bitmap, &info) || info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 ||
      !info.width || !info.height || (size_t)info.width > SIZE_MAX / 4 / info.height)
    goto done;
  size_t size = (size_t)info.width * info.height * 4;
  uint8_t *copy = malloc(size);
  if (!copy)
    goto done;
  void *data = NULL;
  if (AndroidBitmap_lockPixels(e, bitmap, &data)) {
    free(copy);
    goto done;
  }
  for (uint32_t row = 0; row < info.height; row++)
    memcpy(copy + (size_t)row * info.width * 4, (uint8_t *)data + (size_t)row * info.stride,
           (size_t)info.width * 4);
  AndroidBitmap_unlockPixels(e, bitmap);
  *out = (text_bitmap){(int)info.width, (int)info.height, copy, size};
  ok = true;
done:
  android_release(&j);
  return ok;
}
void text_raster_shutdown(void) {
  if (activity_class) {
    android_env j;
    if (android_acquire(&j)) {
      (*j.e)->DeleteGlobalRef(j.e, activity_class);
      android_release(&j);
    }
    activity_class = NULL;
    raster = NULL;
  }
}
