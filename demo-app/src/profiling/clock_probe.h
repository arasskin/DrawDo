#ifndef APP_CLOCK_PROBE_H
#define APP_CLOCK_PROBE_H
#include "base.h"
typedef struct clock_probe {
  int fd;
  uint64_t pending, last;
  uint64_t samples[1024][4];
  size_t count;
} clock_probe;
bool clock_probe_open(clock_probe *, const char *, uint16_t);
void clock_probe_close(clock_probe *);
void clock_probe_pump(clock_probe *);
void clock_probe_json(clock_probe *, FILE *);
#endif
