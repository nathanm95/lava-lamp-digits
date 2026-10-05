#include "digits.h"
#include "fixed.h"

// Each numeral is up to two polylines in a 0..100 box. The box is the stroke
// *centreline*, so the outer edge of the numeral sits half a stroke outside it.
// Vertical strokes always pass through y=50 as an explicit vertex: that keeps
// the sampled points on every numeral aligned with the middle bar, so the bar
// joins without a lump.
#define MAX_PTS 7

typedef struct {
  uint8_t n;
  uint8_t p[MAX_PTS][2];
} Stroke;

typedef struct {
  Stroke s[2];
} Glyph;

static const Glyph GLYPHS[10] = {
  {{{7, {{0, 0}, {100, 0}, {100, 50}, {100, 100}, {0, 100}, {0, 50}, {0, 0}}}, {0, {{0}}}}},
  {{{4, {{20, 22}, {60, 0}, {60, 50}, {60, 100}}}, {0, {{0}}}}},
  {{{6, {{0, 0}, {100, 0}, {100, 50}, {0, 50}, {0, 100}, {100, 100}}}, {0, {{0}}}}},
  {{{5, {{0, 0}, {100, 0}, {100, 50}, {100, 100}, {0, 100}}}, {2, {{22, 50}, {100, 50}}}}},
  {{{3, {{0, 0}, {0, 50}, {100, 50}}}, {3, {{100, 0}, {100, 50}, {100, 100}}}}},
  {{{6, {{100, 0}, {0, 0}, {0, 50}, {100, 50}, {100, 100}, {0, 100}}}, {0, {{0}}}}},
  {{{7, {{100, 0}, {0, 0}, {0, 50}, {0, 100}, {100, 100}, {100, 50}, {0, 50}}}, {0, {{0}}}}},
  {{{3, {{0, 0}, {100, 0}, {38, 100}}}, {0, {{0}}}}},
  {{{7, {{0, 0}, {100, 0}, {100, 50}, {100, 100}, {0, 100}, {0, 50}, {0, 0}}},
    {2, {{0, 50}, {100, 50}}}}},
  {{{7, {{100, 50}, {0, 50}, {0, 0}, {100, 0}, {100, 50}, {100, 100}, {0, 100}}}, {0, {{0}}}}},
};

void digits_layout_init(DigitLayout *L, GRect bounds, bool round) {
  int w = bounds.size.w, h = bounds.size.h;
  L->cx = w / 2;
  L->cy = h / 2;
  if (round) {
    // On a circle the rows must clear the curve at their outer corners, so the
    // numerals are sized off the diameter rather than filling the height.
    L->gap_y = h / 18;
    L->digit_h = h * 35 / 100;
  } else {
    L->gap_y = h / 16;
    L->digit_h = (h - 28 - L->gap_y) / 2;
  }
  L->digit_w = L->digit_h * 62 / 100;
  if (L->digit_w * 2 + L->digit_w * 30 / 100 > w - 20) {
    L->digit_w = (w - 20) * 100 / 230;
  }
  L->gap_x = L->digit_w * 30 / 100;
  L->stroke = L->digit_w * 27 / 100;
  L->spacing = L->stroke * Q4 * 55 / 100;
  L->ball_r = L->stroke * Q4 * 88 / 100;
}

static bool too_close(const PtQ4 *pts, int from, int to, int x, int y, int32_t min_d2) {
  for (int i = from; i < to; i++) {
    int32_t dx = pts[i].x - x, dy = pts[i].y - y;
    if (dx * dx + dy * dy < min_d2) return true;
  }
  return false;
}

static int add_point(PtQ4 *out, int n, int max, int glyph_start, int x, int y, int32_t min_d2) {
  if (n >= max || too_close(out, glyph_start, n, x, y, min_d2)) return n;
  out[n].x = x;
  out[n].y = y;
  return n + 1;
}

// Samples one numeral whose centreline box has its top-left at (left, top),
// all in Q4.
static int build_glyph(const DigitLayout *L, int digit, int left, int top,
                       PtQ4 *out, int n, int max) {
  const Glyph *g = &GLYPHS[digit];
  int cw = (L->digit_w - L->stroke) * Q4;
  int ch = (L->digit_h - L->stroke) * Q4;
  int sp = L->spacing;
  int32_t min_d2 = (sp * 55 / 100) * (sp * 55 / 100);
  int start = n;
  for (int s = 0; s < 2; s++) {
    const Stroke *st = &g->s[s];
    for (int i = 0; i + 1 < st->n; i++) {
      int ax = left + st->p[i][0] * cw / 100, ay = top + st->p[i][1] * ch / 100;
      int bx = left + st->p[i + 1][0] * cw / 100, by = top + st->p[i + 1][1] * ch / 100;
      int32_t dx = bx - ax, dy = by - ay;
      int len = isqrt32(dx * dx + dy * dy);
      int pieces = (len + sp / 2) / sp;
      if (pieces < 1) pieces = 1;
      for (int k = 0; k < pieces; k++) {
        n = add_point(out, n, max, start, ax + dx * k / pieces, ay + dy * k / pieces, min_d2);
      }
      if (i + 2 == st->n) n = add_point(out, n, max, start, bx, by, min_d2);
    }
  }
  return n;
}

// Centreline-box top-left of a numeral centred at (dcx, dcy) px, in Q4.
static int build_centered(const DigitLayout *L, int digit, int dcx, int dcy,
                          PtQ4 *out, int n, int max) {
  int left = (dcx * 2 - (L->digit_w - L->stroke)) * Q4 / 2;
  int top = (dcy * 2 - (L->digit_h - L->stroke)) * Q4 / 2;
  return build_glyph(L, digit, left, top, out, n, max);
}

int digits_build(const DigitLayout *L, int hours, bool two_digit_hours, int minutes,
                 PtQ4 *out, int max) {
  int half = (L->digit_w + L->gap_x) / 2;
  int top_y = L->cy - L->gap_y / 2 - L->digit_h / 2;
  int bot_y = L->cy + (L->gap_y + 1) / 2 + L->digit_h / 2;
  int n = 0;
  if (two_digit_hours) {
    n = build_centered(L, hours / 10, L->cx - half, top_y, out, n, max);
    n = build_centered(L, hours % 10, L->cx + half, top_y, out, n, max);
  } else {
    n = build_centered(L, hours % 10, L->cx, top_y, out, n, max);
  }
  n = build_centered(L, minutes / 10, L->cx - half, bot_y, out, n, max);
  n = build_centered(L, minutes % 10, L->cx + half, bot_y, out, n, max);
  return n;
}
