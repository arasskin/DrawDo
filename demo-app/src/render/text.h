#ifndef APP_TEXT_H
#define APP_TEXT_H
#include "base.h"
enum { LABEL_POOL_SIZE = 65535, LABEL_HANDLE_LIMIT = 65519, LABEL_CAPACITY = 96 };
bool labels_init(void);
bool labels_prepare(int, const char *, float, float, float);
bool labels_prepare_paragraph(int, const char *, float, float, float, float);
void labels_clip(float, float, float, float);
bool labels_draw(int, float, float, uint32_t);
void labels_flush(float, float);
void labels_shutdown(void);
#endif
