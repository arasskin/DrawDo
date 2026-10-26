#ifndef APP_TEXT_RASTER_H
#define APP_TEXT_RASTER_H
#include "base.h"
typedef struct text_bitmap {
  int width, height;
  uint8_t *pixels;
  size_t size;
} text_bitmap;
bool text_raster_init(void);
/* Height zero: ellipsized label. Positive height: wrapped paragraph, clipped to
 * the viewport, using the same metrics as the native multiline editor. */
bool text_raster_render(const char *, float, int width, int height, text_bitmap *);
void text_raster_shutdown(void);
#endif
