// settings_screen.cpp: the settings menu, opened by a long knob press with no
// aircraft selected. Rows: trail length (press cycles 5/10/20/30 min), Wi-Fi
// (opens the Wi-Fi screen), overhead range (press cycles 2/4/8/15/30 km) and
// overhead mode. Both cycling rows save to NVS at once. Rotate moves the
// highlight; a long press closes the menu.
#include "settings_screen.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include "config.h"

namespace {

const char *const NVS_NS = "radar";
const char *const NVS_KEY = "trail_s";
const char *const NVS_KEY_RANGE = "oh_range_m";
const int N_RANGES = 5;
const uint32_t RANGE_M[N_RANGES] = {2000, 4000, 8000, 15000, 30000};
const char *const RANGE_LABEL[N_RANGES] = {"2 km", "4 km", "8 km", "15 km", "30 km"};
const int N_OPTIONS = 4;
const uint32_t OPTION_S[N_OPTIONS] = {300, 600, 1200, 1800};
const char *const OPTION_LABEL[N_OPTIONS] = {"5 min", "10 min", "20 min", "30 min"};
enum Row { ROW_TRAIL, ROW_WIFI, ROW_RANGE, ROW_OVERHEAD, N_ROWS };
const int ROW_W = 252, ROW_H = 36, ROW_Y0 = 30, ROW_STEP = 44;
const int VALUE_W = 54;   // right-hand value column (e.g. "30 km"); labels stop short of it
const uint32_t COL_ACCENT = 0x2F8F5A, COL_TEXT = 0xC8D2DC;

struct Settings {
  UiState *st = nullptr;
  lv_obj_t *row[N_ROWS] = {};
  lv_obj_t *label[N_ROWS] = {};
  lv_obj_t *value = nullptr;         // trail length, right-aligned on its row
  lv_obj_t *range_value = nullptr;   // overhead range, right-aligned on its row
  int highlight = 0;
  void (*on_wifi)() = nullptr;
  void (*on_overhead)() = nullptr;
};
Settings g_set;   // one settings screen per UI

int rangeIndexOf(uint32_t m) {
  for (int i = 0; i < N_RANGES; ++i)
    if (RANGE_M[i] == m) return i;
  return -1;
}

int indexOf(uint32_t s) {
  for (int i = 0; i < N_OPTIONS; ++i)
    if (OPTION_S[i] == s) return i;
  return -1;
}

void paintRows(Settings *c) {
  int t = indexOf(c->st->trail_window_s);
  lv_label_set_text(c->value, OPTION_LABEL[t >= 0 ? t : indexOf(TRAIL_WINDOW_DEFAULT_S)]);
  int rg = rangeIndexOf((uint32_t)lroundf(c->st->overhead_radius_m));
  lv_label_set_text(c->range_value, RANGE_LABEL[rg >= 0 ? rg : 1]);
  for (int i = 0; i < N_ROWS; ++i) {
    bool on = i == c->highlight;
    lv_color_t fg = on ? lv_color_black() : lv_color_hex(COL_TEXT);
    lv_obj_set_style_bg_opa(c->row[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(c->label[i], fg, 0);
    lv_obj_t *v = i == ROW_TRAIL ? c->value : i == ROW_RANGE ? c->range_value : nullptr;
    if (v) lv_obj_set_style_text_color(v, on ? lv_color_black() : lv_color_hex(0x4ADE80), 0);
  }
}

void hidePanel(lv_obj_t *s) { lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN); }

}  // namespace

lv_obj_t *settingsScreenCreate(lv_obj_t *parent, UiState *st) {
  g_set.st = st;
  lv_obj_t *s = lv_obj_create(parent);
  lv_obj_set_size(s, 284, 276);
  lv_obj_center(s);
  lv_obj_set_style_radius(s, 22, 0);
  lv_obj_set_style_bg_color(s, lv_color_hex(0x0C1218), 0);
  lv_obj_set_style_bg_opa(s, 240, 0);
  lv_obj_set_style_border_width(s, 1, 0);
  lv_obj_set_style_border_color(s, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_border_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(s, 0, 0);
  lv_obj_set_style_pad_all(s, 12, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_user_data(s, &g_set);

  lv_obj_t *title = lv_label_create(s);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x3E7A55), 0);
  lv_obj_set_style_text_letter_space(title, 2, 0);
  lv_label_set_text(title, "SETTINGS");

  static const char *const LABELS[N_ROWS] = {LV_SYMBOL_LOOP "  Trail length", LV_SYMBOL_WIFI "  Wi-Fi",
                                             LV_SYMBOL_GPS "  Overhead range", LV_SYMBOL_EYE_OPEN "  Overhead mode"};
  for (int i = 0; i < N_ROWS; ++i) {
    lv_obj_t *r = lv_obj_create(s);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, ROW_W, ROW_H);
    lv_obj_align(r, LV_ALIGN_TOP_MID, 0, ROW_Y0 + i * ROW_STEP);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_pad_hor(r, 12, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_label_create(r);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_label_set_text(l, LABELS[i]);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    bool has_value = i == ROW_TRAIL || i == ROW_RANGE;
    lv_obj_set_width(l, ROW_W - 24 - (has_value ? VALUE_W + 6 : 0));
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    g_set.row[i] = r;
    g_set.label[i] = l;
  }
  g_set.value = lv_label_create(g_set.row[ROW_TRAIL]);
  lv_obj_set_style_text_font(g_set.value, &lv_font_montserrat_14, 0);
  lv_obj_set_width(g_set.value, VALUE_W);
  lv_obj_set_style_text_align(g_set.value, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(g_set.value, LV_ALIGN_RIGHT_MID, 0, 0);
  g_set.range_value = lv_label_create(g_set.row[ROW_RANGE]);
  lv_obj_set_style_text_font(g_set.range_value, &lv_font_montserrat_14, 0);
  lv_obj_set_width(g_set.range_value, VALUE_W);
  lv_obj_set_style_text_align(g_set.range_value, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(g_set.range_value, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t *hint = lv_label_create(s);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(hint, lv_color_hex(0x5B6B7C), 0);
  lv_label_set_text(hint, "Press to select  " LV_SYMBOL_BULLET "  hold to close");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);

  g_set.highlight = ROW_TRAIL;
  paintRows(&g_set);
  return s;
}

void settingsScreenOpen(lv_obj_t *s) {
  if (!s) return;
  Settings *c = static_cast<Settings *>(lv_obj_get_user_data(s));
  c->highlight = ROW_TRAIL;
  paintRows(c);
  lv_obj_move_foreground(s);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_HIDDEN);
  Serial.printf("[settings] open trail_s=%u\n", (unsigned)c->st->trail_window_s);
}

bool settingsScreenHandle(lv_obj_t *s, InputEvent e) {
  if (!settingsScreenIsOpen(s)) return false;
  Settings *c = static_cast<Settings *>(lv_obj_get_user_data(s));
  switch (e) {
    case InputEvent::ROTATE_RIGHT:
      if (c->highlight < N_ROWS - 1) ++c->highlight;
      paintRows(c);
      return true;
    case InputEvent::ROTATE_LEFT:
      if (c->highlight > 0) --c->highlight;
      paintRows(c);
      return true;
    case InputEvent::PRESS:
      if (c->highlight == ROW_TRAIL) {   // cycle and save; the menu stays open
        int t = indexOf(c->st->trail_window_s);
        uint32_t v = OPTION_S[(t + 1) % N_OPTIONS];
        c->st->trail_window_s = v;
        settingsSaveTrailWindow(v);
        paintRows(c);
      } else if (c->highlight == ROW_RANGE) {   // cycle and save; the menu stays open
        int rg = rangeIndexOf((uint32_t)lroundf(c->st->overhead_radius_m));
        uint32_t m = RANGE_M[(rg + 1) % N_RANGES];
        c->st->overhead_radius_m = (float)m;
        settingsSaveOverheadRange(m);
        paintRows(c);
      } else if (c->highlight == ROW_WIFI) {
        hidePanel(s);
        if (c->on_wifi) c->on_wifi();
      } else {
        hidePanel(s);
        if (c->on_overhead) c->on_overhead();
      }
      return true;
    case InputEvent::LONG_PRESS:
      Serial.println("[settings] close");
      hidePanel(s);
      return true;
    default:
      return false;
  }
}

void settingsScreenSetOverheadCallback(void (*cb)()) { g_set.on_overhead = cb; }

void settingsScreenSetWifiCallback(void (*cb)()) { g_set.on_wifi = cb; }

bool settingsScreenIsOpen(lv_obj_t *s) { return s && !lv_obj_has_flag(s, LV_OBJ_FLAG_HIDDEN); }

uint32_t settingsLoadTrailWindow() {
  Preferences prefs;
  uint32_t s = TRAIL_WINDOW_DEFAULT_S;
  if (prefs.begin(NVS_NS, false)) {   // read-write: creates the namespace, so no NOT_FOUND log on first boot
    s = prefs.getUInt(NVS_KEY, TRAIL_WINDOW_DEFAULT_S);
    prefs.end();
  }
  if (indexOf(s) < 0) s = TRAIL_WINDOW_DEFAULT_S;
  Serial.printf("[settings] trail_s=%u (load)\n", (unsigned)s);
  return s;
}

void settingsSaveTrailWindow(uint32_t s) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) {
    Serial.println("[settings] nvs open failed");
    return;
  }
  prefs.putUInt(NVS_KEY, s);
  prefs.end();
  Serial.printf("[settings] trail_s=%u (save)\n", (unsigned)s);
}

uint32_t settingsLoadOverheadRange() {
  Preferences prefs;
  uint32_t m = (uint32_t)OVERHEAD_RADIUS_M;
  if (prefs.begin(NVS_NS, false)) {
    m = prefs.getUInt(NVS_KEY_RANGE, m);
    prefs.end();
  }
  if (rangeIndexOf(m) < 0) m = (uint32_t)OVERHEAD_RADIUS_M;
  Serial.printf("[settings] overhead range=%u m (load)\n", (unsigned)m);
  return m;
}

void settingsSaveOverheadRange(uint32_t m) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) {
    Serial.println("[settings] nvs open failed");
    return;
  }
  prefs.putUInt(NVS_KEY_RANGE, m);
  prefs.end();
  Serial.printf("[settings] overhead range=%u m (save)\n", (unsigned)m);
}
