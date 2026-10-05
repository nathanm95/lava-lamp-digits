#pragma once
#include <pebble.h>

// Integer helpers. There is no FPU, so everything is fixed point.

static inline int isqrt32(int32_t v) {
  if (v <= 0) return 0;
  uint32_t x = (uint32_t)v, r = 0, bit = 1u << 30;
  while (bit > x) bit >>= 2;
  while (bit) {
    if (x >= r + bit) {
      x -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return (int)r;
}

// 0..1024 in, 0..1024 out, eased at both ends.
static inline int smooth1024(int w) {
  if (w <= 0) return 0;
  if (w >= 1024) return 1024;
  return (int)((int32_t)w * w / 1024 * (3072 - 2 * w) / 1024);
}

// Trig angle for a repeating cycle of `period_ms`, offset by `phase`.
static inline int32_t cycle_angle(uint32_t t_ms, uint16_t period_ms, uint16_t phase) {
  return (int32_t)(((t_ms % period_ms) * (uint32_t)TRIG_MAX_ANGLE / period_ms + phase) %
                   TRIG_MAX_ANGLE);
}

// a * sin(angle), a in any unit.
static inline int scaled_sin(int a, int32_t angle) {
  return (int)((int32_t)a * sin_lookup(angle) / TRIG_MAX_RATIO);
}

static inline int scaled_cos(int a, int32_t angle) {
  return (int)((int32_t)a * cos_lookup(angle) / TRIG_MAX_RATIO);
}

static inline int rand_range(int lo, int hi) {
  return lo + rand() % (hi - lo + 1);
}
