#ifndef APP_BASE_H
#define APP_BASE_H
#include "../../containers/bytes.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static inline double minimum(double a, double b) { return a < b ? a : b; }
static inline double maximum(double a, double b) { return a > b ? a : b; }
static inline double clamp(double x, double lo, double hi) { return minimum(maximum(x, lo), hi); }
typedef ct_bytes app_buffer;
static inline bool app_reserve(void *address, size_t *capacity, size_t count, size_t item) {
  return ct_array_reserve(address, capacity, count, item);
}
bool app_append(app_buffer *, const void *, size_t);
bool app_string(app_buffer *, const char *, size_t);
bool app_format(app_buffer *, const char *, ...);
void app_buffer_free(app_buffer *);
static inline const char *app_cstr(const app_buffer *b) {
  return b->data ? (const char *)b->data : "";
}
static inline int64_t app_clock(clockid_t clock) {
  struct timespec t;
  clock_gettime(clock, &t);
  return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static inline int64_t app_now(void) { return app_clock(CLOCK_MONOTONIC); }
static inline int64_t app_us(void) { return app_now() / 1000; }
static inline int64_t app_wall_ms(void) { return app_clock(CLOCK_REALTIME) / 1000000; }
bool app_mkdir(const char *);
bool app_remove_tree(const char *);
bool app_write(const char *, const void *, size_t);
bool app_json(FILE *, const char *, size_t);
static inline uint64_t app_be(const uint8_t *p, unsigned n) {
  uint64_t v = 0;
  while (n--)
    v = (v << 8) | *p++;
  return v;
}
static inline void app_put_be(uint8_t *p, uint64_t n, unsigned bytes) {
  while (bytes) {
    p[--bytes] = (uint8_t)n;
    n >>= 8;
  }
}
bool app_u8(app_buffer *, uint8_t);
bool app_u32(app_buffer *, uint32_t);
bool app_u64(app_buffer *, uint64_t);
bool app_blob(app_buffer *, const void *, size_t);
typedef struct app_reader {
  const uint8_t *data;
  size_t size;
  bool failed;
} app_reader;
const uint8_t *app_take(app_reader *, size_t);
uint64_t app_read(app_reader *, unsigned);
const uint8_t *app_read_blob(app_reader *, size_t *);
#endif
