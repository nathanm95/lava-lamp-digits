#include "settings.h"

#define SETTINGS_KEY 1
#define SETTINGS_VERSION 1

// Stored with a version byte so a later layout change can't misread old bytes.
typedef struct {
  uint8_t version;
  uint8_t lava_argb;
  uint8_t bg_argb;
  uint8_t raise_to_show;
  uint8_t auto_form;
  uint8_t drift_mode;
} StoredSettings;

Settings g_settings;

static void set_defaults(void) {
  g_settings.lava = GColorOrange;
  g_settings.bg = GColorImperialPurple;
  g_settings.raise_to_show = true;
  g_settings.auto_form = AUTO_FORM_EVERY_5;
  g_settings.drift_mode = DRIFT_AFTER_GLANCE;
}

static void save(void) {
  StoredSettings s = {
    .version = SETTINGS_VERSION,
    .lava_argb = g_settings.lava.argb,
    .bg_argb = g_settings.bg.argb,
    .raise_to_show = g_settings.raise_to_show,
    .auto_form = g_settings.auto_form,
    .drift_mode = g_settings.drift_mode,
  };
  persist_write_data(SETTINGS_KEY, &s, sizeof(s));
}

void settings_load(void) {
  set_defaults();
  StoredSettings s;
  if (persist_read_data(SETTINGS_KEY, &s, sizeof(s)) != sizeof(s) ||
      s.version != SETTINGS_VERSION) {
    return;
  }
  g_settings.lava.argb = s.lava_argb;
  g_settings.bg.argb = s.bg_argb;
  g_settings.raise_to_show = s.raise_to_show;
  g_settings.auto_form = s.auto_form;
  g_settings.drift_mode = s.drift_mode;
}

// Clay sends select values as strings and toggles/colours as integers; accept
// either so a Clay version change can't silently break a setting.
static bool read_int(DictionaryIterator *it, uint32_t key, int32_t *out) {
  Tuple *t = dict_find(it, key);
  if (!t) return false;
  if (t->type == TUPLE_CSTRING) {
    *out = atoi(t->value->cstring);
  } else if (t->length == 1) {
    *out = t->type == TUPLE_INT ? t->value->int8 : t->value->uint8;
  } else if (t->length == 2) {
    *out = t->type == TUPLE_INT ? t->value->int16 : t->value->uint16;
  } else {
    *out = t->value->int32;
  }
  return true;
}

bool settings_handle_inbox(DictionaryIterator *iter) {
  bool any = false;
  int32_t v;
  if (read_int(iter, MESSAGE_KEY_LavaColor, &v)) { g_settings.lava = GColorFromHEX(v); any = true; }
  if (read_int(iter, MESSAGE_KEY_BgColor, &v)) { g_settings.bg = GColorFromHEX(v); any = true; }
  if (read_int(iter, MESSAGE_KEY_RaiseToShow, &v)) { g_settings.raise_to_show = v != 0; any = true; }
  if (read_int(iter, MESSAGE_KEY_AutoForm, &v)) {
    g_settings.auto_form = (v == 1 || v == 5 || v == 15) ? v : AUTO_FORM_NEVER;
    any = true;
  }
  if (read_int(iter, MESSAGE_KEY_DriftMode, &v)) {
    g_settings.drift_mode = v ? DRIFT_ALWAYS : DRIFT_AFTER_GLANCE;
    any = true;
  }
  if (any) save();
  return any;
}
