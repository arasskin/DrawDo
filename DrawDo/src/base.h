#ifndef DRAWDO_BASE_H
#define DRAWDO_BASE_H
#include "containers/bytes.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static inline double minimum(double a, double b) { return a < b ? a : b; }
static inline double maximum(double a, double b) { return a > b ? a : b; }
static inline double clamp(double x, double lo, double hi) { return minimum(maximum(x, lo), hi); }
#endif
