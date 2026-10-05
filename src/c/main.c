#include <pebble.h>
#include "lava.h"
#include "settings.h"

#define FRAME_MS 50
// How long the lava keeps drifting after the time melts, before the face
// freezes to save battery (unless the user picked "always drift").
#define DRIFT_AFTER_MS 30000

// Raise detection: the screen has to swing from facing sideways or down to
// facing up within this window. Pebble has no raise event, so this reads the
// accelerometer directly.
#define RAISE_WINDOW_MS 1500
#define RAISE_COOLDOWN_MS 3000

static Window *s_window;
static Layer *s_canvas;
static AppTimer *s_timer;
static int s_drift_left_ms;
static time_t s_last_s;
static uint16_t s_last_ms;
static bool s_raise_subscribed;
static uint64_t s_away_ts, s_raise_ts;
static uint8_t s_view_run;

#ifdef LAVA_DEBUG
static int s_dbg_inputs, s_dbg_raises, s_dbg_render_ms, s_dbg_frames, s_dbg_fps;
static time_t s_dbg_fps_second;
static char s_dbg_text[64];
#endif

static int elapsed_ms(void) {
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  int dt = (int)((s - s_last_s) * 1000 + ms - s_last_ms);
  s_last_s = s;
  s_last_ms = ms;
  // time_ms() fills seconds and milliseconds separately, so a read that
  // straddles a second boundary can be ~1s out in either direction.
  if (dt < 0 || dt > 250) dt = FRAME_MS;
  return dt;
}

static void frame(void *data) {
  s_timer = NULL;
  int dt = elapsed_ms();
  lava_step(dt);
  layer_mark_dirty(s_canvas);
  if (lava_is_resting() && g_settings.drift_mode != DRIFT_ALWAYS) {
    s_drift_left_ms -= dt;
    if (s_drift_left_ms <= 0) return;  // freeze on this frame until something wakes us
  }
  s_timer = app_timer_register(FRAME_MS, frame, NULL);
}

static void wake(void) {
  s_drift_left_ms = DRIFT_AFTER_MS;
  if (!s_timer) {
    elapsed_ms();  // the freeze shouldn't count as elapsed animation time
    s_timer = app_timer_register(FRAME_MS, frame, NULL);
  }
}

static void form_now(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int h = t->tm_hour, m = t->tm_min;
#ifdef LAVA_SHOW_TIME
  h = LAVA_SHOW_TIME / 100;
  m = LAVA_SHOW_TIME % 100;
#endif
  bool h24 = clock_is_24h_style();
  if (!h24) {
    h %= 12;
    if (h == 0) h = 12;
  }
  lava_form_time(h, h24 || h >= 10, m);
  wake();
}

// ------------------------------------------------------------------ input --

static void tick_handler(struct tm *t, TimeUnits changed) {
  bool due = g_settings.auto_form != AUTO_FORM_NEVER && t->tm_min % g_settings.auto_form == 0;
  if (lava_is_showing() || due) form_now();
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
#ifdef LAVA_DEBUG
  s_dbg_inputs++;
#endif
  form_now();
}

static void touch_handler(const TouchEvent *event, void *context) {
  if (event->type != TouchEvent_Touchdown) return;
#ifdef LAVA_DEBUG
  s_dbg_inputs++;
#endif
  form_now();
}

static void accel_data_handler(AccelData *d, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    if (d[i].did_vibrate) continue;
    int x = d[i].x, z = d[i].z;
    uint64_t ts = d[i].timestamp;
    // z is -1000 with the screen facing the sky, ~0 with the arm hanging down.
    if (z > -200) {
      s_away_ts = ts;
      s_view_run = 0;
      continue;
    }
    if (z > -550 || x < -700 || x > 700) {
      s_view_run = 0;
      continue;
    }
    if (s_view_run < 255) s_view_run++;
    // Two settled samples, so a wrist swinging through face-up doesn't count.
    if (s_view_run == 2 && s_away_ts && ts - s_away_ts < RAISE_WINDOW_MS &&
        ts - s_raise_ts > RAISE_COOLDOWN_MS) {
      s_raise_ts = ts;
#ifdef LAVA_DEBUG
      s_dbg_raises++;
#endif
      form_now();
    }
  }
}

static void update_raise_subscription(void) {
  if (g_settings.raise_to_show && !s_raise_subscribed) {
    accel_data_service_subscribe(5, accel_data_handler);
    accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
    s_raise_subscribed = true;
  } else if (!g_settings.raise_to_show && s_raise_subscribed) {
    accel_data_service_unsubscribe();
    s_raise_subscribed = false;
  }
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  if (!settings_handle_inbox(iter)) return;
  lava_set_palette(g_settings.lava, g_settings.bg);
  update_raise_subscription();
  form_now();  // show the new colours off straight away
}

// ----------------------------------------------------------------- window --

static void canvas_update(Layer *layer, GContext *ctx) {
#ifdef LAVA_DEBUG
  time_t s0;
  uint16_t ms0;
  time_ms(&s0, &ms0);
#endif
  lava_render(ctx);
#ifdef LAVA_DEBUG
  time_t s1;
  uint16_t ms1;
  time_ms(&s1, &ms1);
  s_dbg_render_ms = (int)((s1 - s0) * 1000 + ms1 - ms0);
  if (s1 != s_dbg_fps_second) {
    s_dbg_fps = s_dbg_frames;
    s_dbg_frames = 0;
    s_dbg_fps_second = s1;
  }
  s_dbg_frames++;
  snprintf(s_dbg_text, sizeof(s_dbg_text), "%dfps %dms\nph%d n%d in%d r%d", s_dbg_fps,
           s_dbg_render_ms, lava_debug_phase(), lava_debug_targets(), s_dbg_inputs, s_dbg_raises);
  GRect b = layer_get_bounds(layer);
  GRect box = GRect(b.size.w / 2 - 70, b.size.h / 2 - 30, 140, 60);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, box, 4, GCornersAll);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_dbg_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), box,
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
#endif
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
  lava_init(bounds, PBL_IF_ROUND_ELSE(true, false));
  lava_set_palette(g_settings.lava, g_settings.bg);
  form_now();
}

static void window_unload(Window *window) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  layer_destroy(s_canvas);
}

static void init(void) {
  srand(time(NULL));
  settings_load();
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  accel_tap_service_subscribe(accel_tap_handler);
#if defined(PBL_TOUCH)
  touch_service_subscribe(touch_handler, NULL);
#endif
  update_raise_subscription();

  app_message_register_inbox_received(inbox_received);
  app_message_open(128, 64);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
#if defined(PBL_TOUCH)
  touch_service_unsubscribe();
#endif
  if (s_raise_subscribed) accel_data_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
