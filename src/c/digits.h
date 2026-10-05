#pragma once
#include <pebble.h>

// Positions throughout are Q4 fixed point (1/16 px) so balls can move by less
// than a pixel per frame without the outline stepping.
#define Q4 16

typedef struct {
  int16_t x, y;
} PtQ4;

// Where the two rows of the stacked clock sit, derived from the screen once.
typedef struct {
  int16_t cx, cy;     // screen centre, px
  int16_t digit_w;    // outer box of one numeral, px
  int16_t digit_h;
  int16_t gap_x;      // between the two numerals of a row
  int16_t gap_y;      // between the hours row and the minutes row
  int16_t stroke;     // intended stroke thickness, px
  int16_t spacing;    // distance between sampled points along a stroke, Q4
  int16_t ball_r;     // influence radius of a digit ball, Q4
} DigitLayout;

void digits_layout_init(DigitLayout *L, GRect bounds, bool round);

// Samples the strokes of the stacked time (hours over minutes) into points,
// one per ball. Returns how many were written (never more than max).
int digits_build(const DigitLayout *L, int hours, bool two_digit_hours, int minutes,
                 PtQ4 *out, int max);
