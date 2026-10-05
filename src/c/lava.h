#pragma once
#include <pebble.h>

void lava_init(GRect bounds, bool round);
void lava_set_palette(GColor lava, GColor bg);

// Advances the simulation by dt milliseconds.
void lava_step(int dt_ms);
void lava_render(GContext *ctx);

// Pours the lava into the shape of this time. Asking again while it is formed
// keeps it formed for longer; asking with a different time melts and re-forms.
void lava_form_time(int hours, bool two_digit_hours, int minutes);

// True when the lava is drifting freely, i.e. nothing would be cut off by
// freezing the animation now.
bool lava_is_resting(void);

// True while the time is forming or formed (not melting).
bool lava_is_showing(void);

#ifdef LAVA_DEBUG
int lava_debug_phase(void);
int lava_debug_targets(void);
#endif
