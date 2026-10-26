#ifndef ACCESS_NATIVE_H
#define ACCESS_NATIVE_H
#include "accessible_ui/access.h"
bool platform_accessibility_set(const access_snapshot *);
bool platform_accessibility_send(uint64_t, unsigned);
#if defined(__APPLE__)
void access_apple_init(void *window);
#endif
#endif
