#ifndef DRAWDO_PLATFORM_H
#define DRAWDO_PLATFORM_H
#include "text_buffers/editor.h"
const char *platform_init(void);
editor_rect platform_bounds(void);
float platform_scale(void);
void platform_error(const char *);
void platform_shutdown(void);
#endif
