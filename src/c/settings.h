#pragma once
#include <pebble.h>

// How often the lava forms the time without being asked.
typedef enum {
  AUTO_FORM_NEVER = 0,
  AUTO_FORM_EVERY_MIN = 1,
  AUTO_FORM_EVERY_5 = 5,
  AUTO_FORM_EVERY_15 = 15,
} AutoForm;

typedef enum {
  DRIFT_AFTER_GLANCE = 0,  // animate for a while after each glance, then freeze
  DRIFT_ALWAYS = 1,        // never freeze (costs a lot of battery)
} DriftMode;

typedef struct {
  GColor lava;
  GColor bg;
  bool raise_to_show;
  uint8_t auto_form;   // AutoForm, minutes
  uint8_t drift_mode;  // DriftMode
} Settings;

extern Settings g_settings;

void settings_load(void);
// Applies an AppMessage from the Clay page. Returns true if anything was read.
bool settings_handle_inbox(DictionaryIterator *iter);
