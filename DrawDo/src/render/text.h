#ifndef APP_TEXT_H
#define APP_TEXT_H
#include "base.h"
enum { LABEL_POOL_SIZE = 512, LABEL_HANDLE_LIMIT = 480, LABEL_CAPACITY = 128 };
bool labels_init(void);
void labels_begin(void);
float labels_height(int);
bool labels_prepare(int, const char *, float, float, float);
bool labels_prepare_paragraph(int, const char *, float, float, float, float);
void labels_clip(float, float, float, float);
bool labels_draw(int, float, float, uint32_t);
void labels_flush(float, float);
void labels_shutdown(void);
#endif
