// settings_screen.cpp: a 260 x 200 centred panel picking the trail length; the choice lives in NVS.
#include "settings_screen.h"
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

namespace {

const char *const NVS_NS = "radar";
const char *const NVS_KEY = "trail_s";
const int N_OPTIONS = 4;
const uint32_t OPTION_S[N_OPTIONS] = {300, 600, 1200, 1800};
const char *const OPTION_LABEL[N_OPTIONS] = {"5 min", "10 min", "20 min", "30 min"};
const int ROW_W = 200, ROW_H = 30, ROW_Y0 = 34, ROW_STEP = 36;

struct Settings {
  UiState *st = nullptr;
  lv_obj_t *row[N_OPTIONS] = {};
  int highlight = 0;
};
Settings g_set;   // one settings screen per UI

int indexOf(uint32_t s) {
  for (int i = 0; i < N_OPTIONS; ++i)
    if (OPTION_S[i] == s) return i;
  return -1;
}

void paintRows(Settings *c) {
  for (int i = 0; i < N_OPTIONS; ++i) {
    bool on = i == c->highlight;
    lv_obj_set_style_bg_opa(c->row[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(c->row[i], on ? lv_color_black() : lv_color_hex(0xC8D2DC), 0);
  }
}

void hidePanel(lv_obj_t *s) { lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN); }

}  // namespace

lv_obj_t *settingsScreenCreate(lv_obj_t *parent, UiState *st) {
  g_set.st = st;
  lv_obj_t *s = lv_obj_create(parent);
  lv_obj_set_size(s, 260, 200);
  lv_obj_center(s);
  lv_obj_set_style_radius(s, 18, 0);
  lv_obj_set_style_bg_color(s, lv_color_hex(0x0C1218), 0);
  lv_obj_set_style_bg_opa(s, 235, 0);
  lv_obj_set_style_border_width(s, 1, 0);
  lv_obj_set_style_border_color(s, lv_color_hex(0x2F8F5A), 0);
  lv_obj_set_style_border_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(s, 0, 0);
  lv_obj_set_style_pad_all(s, 10, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_user_data(s, &g_set);

  lv_obj_t *title = lv_label_create(s);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x3E7A55), 0);
  lv_label_set_text(title, "TRAIL LENGTH");

  for (int i = 0; i < N_OPTIONS; ++i) {
    lv_obj_t *r = lv_label_create(s);
    lv_obj_set_size(r, ROW_W, ROW_H);
    lv_obj_align(r, LV_ALIGN_TOP_MID, 0, ROW_Y0 + i * ROW_STEP);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(0x2F8F5A), 0);
    lv_obj_set_style_pad_top(r, (ROW_H - 18) / 2, 0);   // centre the 16 px font's ~18 px line vertically
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_16, 0);
    lv_label_set_text(r, OPTION_LABEL[i]);
    g_set.row[i] = r;
  }
  int h = indexOf(st ? st->trail_window_s : TRAIL_WINDOW_DEFAULT_S);
  g_set.highlight = h >= 0 ? h : indexOf(TRAIL_WINDOW_DEFAULT_S);
  paintRows(&g_set);
  return s;
}

void settingsScreenOpen(lv_obj_t *s) {
  if (!s) return;
  Settings *c = static_cast<Settings *>(lv_obj_get_user_data(s));
  int h = indexOf(c->st->trail_window_s);
  c->highlight = h >= 0 ? h : indexOf(TRAIL_WINDOW_DEFAULT_S);
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
      if (c->highlight < N_OPTIONS - 1) ++c->highlight;
      paintRows(c);
      return true;
    case InputEvent::ROTATE_LEFT:
      if (c->highlight > 0) --c->highlight;
      paintRows(c);
      return true;
    case InputEvent::PRESS: {
      uint32_t v = OPTION_S[c->highlight];
      c->st->trail_window_s = v;
      settingsSaveTrailWindow(v);
      c->st->settings_open = false;
      hidePanel(s);
      return true;
    }
    case InputEvent::LONG_PRESS:
      Serial.println("[settings] cancel");
      c->st->settings_open = false;
      hidePanel(s);
      return true;
    default:
      return false;
  }
}

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
