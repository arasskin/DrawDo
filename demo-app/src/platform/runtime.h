#ifndef APP_PLATFORM_H
#define APP_PLATFORM_H
#include "base.h"
typedef struct platform_insets {
  float top, bottom;
} platform_insets;
platform_insets platform_safe_area(void);
const char *platform_directory(void);
void platform_keep_awake(bool);
void platform_log(bool, const char *, const char *, ...);
void platform_profile_metadata(FILE *);
bool platform_profile_options(int *, int *, int *, int *);
#endif
