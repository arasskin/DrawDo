#ifndef TEXT_BENCH_PROBE_H
#define TEXT_BENCH_PROBE_H
#include <stdint.h>
typedef void (*bench_callback)(uint32_t);
int bench_start(bench_callback);
uint32_t bench_latest(void);
int bench_save(const char *path);
#endif
