#ifndef CRDT_DEMO_TIMING_H
#define CRDT_DEMO_TIMING_H
#include "app.h"
#include "text_buffers/trace.h"
const char *timing_directory(const char *base);
void timing_start(const char *directory);
bool timing_tick(app *);
void timing_save(void);
uint64_t timing_timeout(void);
#endif
