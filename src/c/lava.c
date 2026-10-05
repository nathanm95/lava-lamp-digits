#include "lava.h"
#include "digits.h"
#include "fixed.h"

// The lava is a set of metaballs. Most of them are packed into a few blobs
// that bob up and down like wax in a lamp; the rest form a pool along the
// bottom. To show the time, each ball is given a point on the numerals'
// strokes and blends from where its blob would put it to that point. Because
// the field is summed, balls packed along a stroke fuse into a solid numeral,
// and balls leaving a blob stretch it into streams on the way.

#define NUM_BLOBS 5
#define BLOB_BALLS 120
#define POOL_BALLS 30
#define MAX_BALLS (BLOB_BALLS + POOL_BALLS)
#define MAX_W 260

static const uint8_t BLOB_SIZES[NUM_BLOBS] = {32, 28, 24, 20, 16};

// Field thresholds. A lone ball adds 256 at its centre, falling smoothly to 0
// at its radius. RIM is a one-pixel blend between lava and liquid that stands
// in for anti-aliasing; CORE is only reached inside the packed blobs, so they
// glow while the thinner numerals stay a flat colour.
#define T_RIM 150
#define T_MAIN 185
#define T_CORE 1500

#define GATHER_DELAY_MAX 350
#define GATHER_MOVE_MS 1100
#define HOLD_MS 4500
#define MELT_DELAY_MAX 700
#define MELT_LAG_MS 250
#define MELT_MOVE_MS 1500

typedef enum { PH_DRIFT, PH_GATHER, PH_HOLD, PH_MELT } Phase;
typedef enum { ROLE_FREE, ROLE_DIGIT, ROLE_SINK } Role;

typedef struct {
  int16_t xc, yc;            // centre of travel, px
  int16_t ax, ay1, ay2;      // travel amplitudes, px
  uint16_t px, py1, py2, pb; // periods, ms (pb = breathing)
  uint16_t phx, phy1, phy2, phb;
  int16_t radius;            // packed radius, Q4
  // Recomputed every step.
  int32_t cx, cy;            // Q4
  int16_t stretch;           // 0..1024, how fast it is travelling vertically
  int16_t breathe;           // -1024..1024
  int8_t dir;                // -1 rising, +1 sinking
} Blob;

typedef struct {
  int16_t ox, oy;            // blob balls: offset from blob centre. pool: home position. Q4
  uint16_t jig_phase, jig_period;
  int16_t tx, ty;            // formation target, Q4
  int16_t w;                 // 0..1024 blend from drift position to target
  int16_t clock;             // ms since this ball's phase began
  int16_t delay;             // ms before it starts moving in this phase
  int16_t droop, droop_v;    // melt sag (Q4) and its speed (Q4/s)
  int16_t drip_g;            // melt gravity, Q4/s^2
  uint8_t role;
  uint8_t blob;
  int16_t x, y, r;           // output, Q4
} Ball;

static int s_w, s_h;
static bool s_round;
static DigitLayout s_layout;
static Blob s_blobs[NUM_BLOBS];
static Ball s_balls[MAX_BALLS];
static uint8_t s_order[MAX_BALLS];
static int32_t s_r2[MAX_BALLS], s_k[MAX_BALLS];
static uint16_t s_acc[MAX_W];
static PtQ4 s_targets[MAX_BALLS];
static int s_num_targets;
static int16_t s_drift_r, s_pool_r, s_max_r;

static uint32_t s_t;             // animation clock, ms
static Phase s_phase;
static int s_phase_ms;
static int s_shown_key = -1;     // time formed or forming
static int s_pending_key = -1;   // time to form once the current melt ends
static int s_pool_sink;          // 0..1024: the pool drops away while the time shows

static GColor s_bg, s_glow, s_rim, s_main, s_core;

// --------------------------------------------------------------- geometry --

// Half the visible width of the row at y (px). Rect screens are uniform.
static int half_chord(int y) {
  if (!s_round) return s_w / 2;
  int r = s_w / 2, dy = y - s_h / 2;
  if (dy <= -r || dy >= r) return 0;
  return isqrt32(r * r - dy * dy);
}

// Bottom edge of the glass under x (Q4), for laying the pool along it.
static int floor_y(int xq) {
  if (!s_round) return s_h * Q4;
  int r = (s_w / 2) * Q4, dx = xq - r;
  if (dx <= -r || dx >= r) return (s_h / 2) * Q4;
  return (s_h / 2) * Q4 + isqrt32(r * r - dx * dx);
}

static int pool_x(int i) {
  if (!s_round) return (i * 2 + 1) * s_w * Q4 / (POOL_BALLS * 2);
  // Keep the pool off the steep sides of the circle, where it would climb the wall.
  int span = s_w * 84 / 100;
  return ((s_w - span) / 2) * Q4 + (i * 2 + 1) * span * Q4 / (POOL_BALLS * 2);
}

static int pool_home_y(int xq) { return floor_y(xq) - 2 * Q4; }

// ------------------------------------------------------------------- init --

void lava_init(GRect bounds, bool round) {
  s_w = bounds.size.w;
  s_h = bounds.size.h;
  s_round = round;
  digits_layout_init(&s_layout, bounds, round);
  // Blob balls are bigger and packed tighter than digit balls: the more they
  // overlap, the smoother the blob's outline, instead of a ring of bumps.
  s_drift_r = s_layout.ball_r * 130 / 100;
  s_pool_r = s_layout.ball_r;
  s_max_r = s_layout.ball_r > s_drift_r ? s_layout.ball_r : s_drift_r;

  int sp = s_layout.spacing * 65 / 100;

  uint8_t slot[NUM_BLOBS];
  for (int i = 0; i < NUM_BLOBS; i++) slot[i] = i;
  for (int i = NUM_BLOBS - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    uint8_t t = slot[i]; slot[i] = slot[j]; slot[j] = t;
  }

  int idx = 0;
  for (int b = 0; b < NUM_BLOBS; b++) {
    Blob *B = &s_blobs[b];
    int n = BLOB_SIZES[b];
    B->radius = isqrt32(n * sp * sp * 100 / 314);
    int r_px = B->radius / Q4;
    B->xc = (slot[b] * 2 + 1) * s_w / (NUM_BLOBS * 2);
    B->ax = rand_range(s_w / 12, s_w / 7);
    int top = r_px + 6, bottom = s_h - r_px / 2;
    B->yc = (top + bottom) / 2;
    int amp = (bottom - top) / 2;
    B->ay1 = amp * 3 / 4;
    B->ay2 = amp - B->ay1;
    B->py1 = rand_range(22000, 34000);
    B->py2 = rand_range(7000, 11000);
    B->px = rand_range(12000, 21000);
    B->pb = rand_range(2600, 4200);
    B->phy1 = rand() & 0xFFFF;
    B->phy2 = rand() & 0xFFFF;
    B->phx = rand() & 0xFFFF;
    B->phb = rand() & 0xFFFF;

    // Sunflower packing: even density out to the blob's radius.
    for (int k = 0; k < n; k++, idx++) {
      Ball *ball = &s_balls[idx];
      ball->blob = b;
      int rad = isqrt32((int32_t)B->radius * B->radius * (2 * k + 1) / (2 * n));
      int32_t ang = (int32_t)k * 25033 % TRIG_MAX_ANGLE;  // golden angle
      ball->ox = scaled_cos(rad, ang);
      ball->oy = scaled_sin(rad, ang);
      ball->jig_phase = rand() & 0xFFFF;
      ball->jig_period = rand_range(1800, 3400);
    }
  }
  for (int i = 0; i < POOL_BALLS; i++, idx++) {
    Ball *ball = &s_balls[idx];
    ball->ox = pool_x(i);
    ball->oy = pool_home_y(ball->ox);
    ball->jig_phase = rand() & 0xFFFF;
    ball->jig_period = rand_range(3000, 5000);
  }
  for (int i = 0; i < MAX_BALLS; i++) s_order[i] = i;
  s_phase = PH_DRIFT;
  s_t = rand() % 60000;  // a different arrangement every launch
  lava_step(0);          // place every ball before anything measures from them
}

static GColor rgb(int r, int g, int b) {
  r = r < 0 ? 0 : r > 3 ? 3 : r;
  g = g < 0 ? 0 : g > 3 ? 3 : g;
  b = b < 0 ? 0 : b > 3 ? 3 : b;
  return (GColor){.argb = (uint8_t)(0xC0 | r << 4 | g << 2 | b)};
}

// Channel midpoint, rounding toward the first argument so the rim leans to the
// lava and a one-step difference doesn't vanish into the liquid.
static int mid(int a, int b) { return (a + b + (a > b ? 1 : 0)) / 2; }

void lava_set_palette(GColor lava, GColor bg) {
  s_bg = bg;
  s_main = lava;
  s_core = rgb(lava.r + 1, lava.g + 1, lava.b + 1);
  s_rim = rgb(mid(lava.r, bg.r), mid(lava.g, bg.g), mid(lava.b, bg.b));
  s_glow = rgb(bg.r + 1, bg.g + 1, bg.b + 1);
}

// -------------------------------------------------------------- formation --

static void start_gather(int key) {
  int hours = (key % 10000) / 100, minutes = key % 100;
  bool two = key >= 10000;
  s_num_targets = digits_build(&s_layout, hours, two, minutes, s_targets, MAX_BALLS);

  // Greedy nearest match, visiting targets in a shuffled order so the few
  // long journeys at the end are spread across the face instead of all
  // landing on the last numeral.
  static uint8_t ord[MAX_BALLS];
  static bool used[MAX_BALLS];
  for (int i = 0; i < s_num_targets; i++) ord[i] = i;
  for (int i = s_num_targets - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    uint8_t t = ord[i]; ord[i] = ord[j]; ord[j] = t;
  }
  memset(used, 0, sizeof(used));
  // The pool only joins in when the blobs alone can't cover the strokes.
  int limit = s_num_targets <= BLOB_BALLS ? BLOB_BALLS : MAX_BALLS;
  for (int k = 0; k < s_num_targets; k++) {
    const PtQ4 *t = &s_targets[ord[k]];
    int best = -1;
    int32_t best_d = INT32_MAX;
    for (int i = 0; i < limit; i++) {
      if (used[i]) continue;
      int32_t dx = s_balls[i].x - t->x, dy = s_balls[i].y - t->y;
      int32_t d = dx * dx + dy * dy;
      if (d < best_d) { best_d = d; best = i; }
    }
    if (best < 0) break;
    used[best] = true;
    Ball *b = &s_balls[best];
    b->role = ROLE_DIGIT;
    b->tx = t->x;
    b->ty = t->y;
  }
  // Wax that isn't needed drains into the pool so it can't clutter the numerals.
  for (int i = 0; i < MAX_BALLS; i++) {
    Ball *b = &s_balls[i];
    if (!used[i] && i < BLOB_BALLS) {
      b->role = ROLE_SINK;
      int x = b->x;
      int lo = (s_w / 2 - half_chord(s_h - 8) + 6) * Q4, hi = (s_w / 2 + half_chord(s_h - 8) - 6) * Q4;
      b->tx = x < lo ? lo : x > hi ? hi : x;
      b->ty = pool_home_y(b->tx) + 2 * Q4;
    }
    if (b->role != ROLE_FREE) {
      b->w = 0;
      b->clock = 0;
      b->delay = rand_range(0, GATHER_DELAY_MAX);
      b->droop = 0;
      b->droop_v = 0;
    }
  }
  s_phase = PH_GATHER;
  s_phase_ms = 0;
  s_shown_key = key;
}

static void start_melt(void) {
  for (int i = 0; i < MAX_BALLS; i++) {
    Ball *b = &s_balls[i];
    if (b->role == ROLE_FREE) continue;
    b->clock = 0;
    b->delay = rand_range(0, MELT_DELAY_MAX);
    b->droop_v = 0;
    b->drip_g = rand_range(20, 70) * Q4;
  }
  s_phase = PH_MELT;
  s_phase_ms = 0;
}

void lava_form_time(int hours, bool two_digit_hours, int minutes) {
  int key = (two_digit_hours ? 10000 : 0) + hours * 100 + minutes;
  switch (s_phase) {
    case PH_DRIFT:
      start_gather(key);
      break;
    case PH_GATHER:
      if (key != s_shown_key) s_pending_key = key;
      break;
    case PH_HOLD:
      if (key == s_shown_key) {
        s_phase_ms = 0;
      } else {
        s_pending_key = key;
        start_melt();
      }
      break;
    case PH_MELT:
      if (key == s_shown_key) {
        // Pull the melting numerals straight back together.
        for (int i = 0; i < MAX_BALLS; i++) {
          if (s_balls[i].role == ROLE_FREE) continue;
          s_balls[i].clock = 0;
          s_balls[i].delay = 0;
        }
        s_phase = PH_GATHER;
        s_phase_ms = 0;
      } else {
        s_pending_key = key;
      }
      break;
  }
}

bool lava_is_resting(void) { return s_phase == PH_DRIFT; }

bool lava_is_showing(void) { return s_phase == PH_GATHER || s_phase == PH_HOLD; }

// ----------------------------------------------------------------- update --

static void step_blobs(void) {
  for (int b = 0; b < NUM_BLOBS; b++) {
    Blob *B = &s_blobs[b];
    int32_t ay = cycle_angle(s_t, B->py1, B->phy1);
    int32_t y = B->yc * Q4 + scaled_cos(B->ay1 * Q4, ay) +
                scaled_cos(B->ay2 * Q4, cycle_angle(s_t, B->py2, B->phy2));
    int32_t x = B->xc * Q4 + scaled_sin(B->ax * Q4, cycle_angle(s_t, B->px, B->phx));
    // y follows cos, so it is rising whenever sin is positive.
    int32_t s = sin_lookup(ay);
    B->dir = s > 0 ? -1 : 1;
    B->stretch = (s < 0 ? -s : s) >> 6;
    B->breathe = scaled_sin(1024, cycle_angle(s_t, B->pb, B->phb));

    int keep = s_round ? half_chord(y / Q4) * Q4 - B->radius * 70 / 100
                       : s_w * Q4 / 2 - B->radius * 60 / 100;
    if (keep < 0) keep = 0;
    int cx = s_w * Q4 / 2;
    if (x < cx - keep) x = cx - keep;
    if (x > cx + keep) x = cx + keep;
    B->cx = x;
    B->cy = y;
  }
}

static void step_phase(int dt) {
  bool done = true;
  switch (s_phase) {
    case PH_DRIFT:
      break;

    case PH_GATHER:
      for (int i = 0; i < MAX_BALLS; i++) {
        Ball *b = &s_balls[i];
        if (b->role == ROLE_FREE) continue;
        if (b->clock < 30000) b->clock += dt;
        if (b->droop) b->droop = b->droop * 3 / 4;
        b->droop_v = 0;
        if (b->clock < b->delay) { done = false; continue; }
        b->w += dt * 1024 / GATHER_MOVE_MS;
        if (b->w >= 1024) b->w = 1024; else done = false;
      }
      if (done) {
        s_phase = PH_HOLD;
        s_phase_ms = 0;
      }
      break;

    case PH_HOLD:
      if (s_pending_key >= 0 && s_pending_key != s_shown_key) {
        start_melt();
      }
#ifndef LAVA_HOLD
      else if (s_phase_ms >= HOLD_MS) {
        start_melt();
      }
#endif
      break;

    case PH_MELT:
      for (int i = 0; i < MAX_BALLS; i++) {
        Ball *b = &s_balls[i];
        if (b->role == ROLE_FREE) continue;
        if (b->clock < 30000) b->clock += dt;
        if (b->clock < b->delay) { done = false; continue; }
        // Sag first, then let go: the numerals droop before they dissolve.
        b->droop_v += b->drip_g * dt / 1000;
        b->droop += b->droop_v * dt / 1000;
        if (b->clock < b->delay + MELT_LAG_MS) { done = false; continue; }
        b->w -= dt * 1024 / MELT_MOVE_MS;
        if (b->w <= 0) b->w = 0; else done = false;
      }
      if (done) {
        for (int i = 0; i < MAX_BALLS; i++) {
          s_balls[i].role = ROLE_FREE;
          s_balls[i].droop = 0;
        }
        s_phase = PH_DRIFT;
        s_shown_key = -1;
        if (s_pending_key >= 0) {
          int key = s_pending_key;
          s_pending_key = -1;
          start_gather(key);
        }
      }
      break;
  }

  int sink_goal = (s_phase == PH_GATHER || s_phase == PH_HOLD) ? 1024 : 0;
  int step = dt * 1024 / 900;
  if (s_pool_sink < sink_goal) s_pool_sink = s_pool_sink + step > sink_goal ? sink_goal : s_pool_sink + step;
  if (s_pool_sink > sink_goal) s_pool_sink = s_pool_sink - step < sink_goal ? sink_goal : s_pool_sink - step;
}

static void step_balls(void) {
  for (int i = 0; i < MAX_BALLS; i++) {
    Ball *b = &s_balls[i];
    int32_t x, y;
    int r;
    if (i < BLOB_BALLS) {
      const Blob *B = &s_blobs[b->blob];
      int sx = 1024 - B->stretch * 12 / 100 + B->breathe * 5 / 100;
      int sy = 1024 + B->stretch * 25 / 100 - B->breathe * 5 / 100;
      // The trailing side of a travelling blob draws out into a tail.
      if ((b->oy > 0 && B->dir < 0) || (b->oy < 0 && B->dir > 0)) sy += B->stretch * 30 / 100;
      int32_t ja = cycle_angle(s_t, b->jig_period, b->jig_phase);
      x = B->cx + b->ox * sx / 1024 + scaled_sin(20, ja);
      y = B->cy + b->oy * sy / 1024 + scaled_cos(20, ja);
      r = s_drift_r;
    } else {
      x = b->ox;
      uint16_t along = (uint16_t)(b->ox * 37);
      y = b->oy + scaled_sin(2 * Q4, cycle_angle(s_t, 4200, along)) +
          scaled_sin(Q4, cycle_angle(s_t, 2900, (uint16_t)(b->jig_phase + along * 2))) +
          s_pool_sink * 5 * Q4 / 1024;
      r = s_pool_r;
    }

    if (b->role != ROLE_FREE && b->w > 0) {
      int w = smooth1024(b->w);
      int32_t tx = b->tx, ty = b->ty + b->droop;
      int tr = b->role == ROLE_DIGIT ? s_layout.ball_r : s_pool_r * 85 / 100;
      if (b->role == ROLE_DIGIT && s_phase == PH_HOLD) {
        // A barely-there wobble keeps the held numerals looking molten.
        int32_t ja = cycle_angle(s_t, b->jig_period, b->jig_phase);
        tx += scaled_sin(6, ja);
        ty += scaled_cos(6, ja);
      }
      x += (tx - x) * w / 1024;
      y += (ty - y) * w / 1024;
      r += (tr - r) * w / 1024;
    }
    b->x = x;
    b->y = y;
    b->r = r;
  }
}

void lava_step(int dt_ms) {
  s_t += dt_ms;
  s_phase_ms += dt_ms;
  step_blobs();
  step_phase(dt_ms);
  step_balls();
}

// ----------------------------------------------------------------- render --

// 4x4 ordered dither, used for the lamp's glow towards the bottom.
static const uint8_t BAYER[4][4] = {
  {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5},
};

void lava_render(GContext *ctx) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;

  // Sort by top edge so each row only visits balls that can reach it. The
  // order barely changes between frames, so insertion sort is near linear.
  for (int i = 0; i < MAX_BALLS; i++) {
    int32_t r = s_balls[i].r;
    s_r2[i] = r * r;
    s_k[i] = s_r2[i] > 0 ? (256 << 16) / s_r2[i] : 0;
  }
  for (int i = 1; i < MAX_BALLS; i++) {
    uint8_t v = s_order[i];
    int top = s_balls[v].y - s_balls[v].r;
    int j = i - 1;
    while (j >= 0 && s_balls[s_order[j]].y - s_balls[s_order[j]].r > top) {
      s_order[j + 1] = s_order[j];
      j--;
    }
    s_order[j + 1] = v;
  }

  uint8_t bg = s_bg.argb, glow = s_glow.argb;
  uint8_t rim = s_rim.argb, main = s_main.argb, core = s_core.argb;
  int glow_top = s_h * 72 / 100;
  int first = 0;

  for (int y = 0; y < s_h; y++) {
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    int x0 = row.min_x, x1 = row.max_x;
    if (x1 < x0) continue;
    if (x1 >= MAX_W) x1 = MAX_W - 1;
    memset(&s_acc[x0], 0, (x1 - x0 + 1) * sizeof(uint16_t));

    int yq = y * Q4 + Q4 / 2;
    while (first < MAX_BALLS &&
           s_balls[s_order[first]].y - s_balls[s_order[first]].r + 2 * s_max_r < yq) {
      first++;
    }
    for (int k = first; k < MAX_BALLS; k++) {
      int i = s_order[k];
      const Ball *b = &s_balls[i];
      if (b->y - b->r > yq) break;
      int32_t dy = yq - b->y;
      int32_t rem = s_r2[i] - dy * dy;
      if (rem <= 0) continue;
      int xa = (b->x - b->r) >> 4, xb = (b->x + b->r) >> 4;
      if (xa < x0) xa = x0;
      if (xb > x1) xb = x1;
      int32_t dx = xa * Q4 + Q4 / 2 - b->x;
      int32_t q = rem - dx * dx;
      int32_t kk = s_k[i];
      for (int x = xa; x <= xb; x++) {
        if (q > 0) {
          int32_t t = (q * kk) >> 16;
          s_acc[x] += (t * t) >> 8;
        }
        q -= 32 * dx + 256;
        dx += Q4;
      }
    }

    int glow_level = y > glow_top ? (y - glow_top) * 5 / (s_h - glow_top) : -1;
    const uint8_t *bayer = BAYER[y & 3];
    uint8_t *d = row.data;
    for (int x = x0; x <= x1; x++) {
      uint16_t v = s_acc[x];
      if (v >= T_MAIN) d[x] = v >= T_CORE ? core : main;
      else if (v >= T_RIM) d[x] = rim;
      else d[x] = bayer[x & 3] < glow_level ? glow : bg;
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

#ifdef LAVA_DEBUG
int lava_debug_phase(void) { return s_phase; }
int lava_debug_targets(void) { return s_num_targets; }
#endif
