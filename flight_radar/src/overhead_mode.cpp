// overhead_mode.cpp: a black screen showing only the nearest airborne aircraft
// within the chosen range (st->overhead_radius_m) of home, drawn where it is (the
// screen's edge is that far away, home is the centre) and pointing along its track, with a
// compact flight card next to it. With nothing that close, only a dim line of text.
//
// The plane is a yellow 52 px silhouette over a softly pulsing yellow glow, both
// baked by tools/gen_plane_sprite.py on one canvas (plane_halo) so they rotate
// together. They are expanded once to TRUE_COLOR_ALPHA (in PSRAM), since LVGL 8.4
// cannot rotate ALPHA_8BIT images (see aircraft_layer.cpp).
#include "overhead_mode.h"
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "config.h"
#include "esp_heap_caps.h"
#include "geo.h"
#include "metadata_table.h"
#include "selection.h"
#include "sprites/plane_halo.h"
#include "units.h"

namespace {

const lv_coord_t PLANE_PX = PLANE_HALO_CANVAS;      // canvas, glow included
const float PLANE_HALF = PLANE_HALO_PLANE / 2.f;    // half the visible plane
const lv_coord_t CARD_W = 184, CARD_H = 104;
const lv_coord_t CARD_GAP = 16;   // between the plane's edge and the card
const uint32_t COL_PLANE = 0xFDE047, COL_GLOW = 0xFACC15;
const uint8_t GLOW_OPA_MIN = 120, GLOW_OPA_MAX = 255;
const uint32_t GLOW_PULSE_MS = 1400;
const uint32_t COL_CARD = 0x0B1117, COL_BORDER = 0x24543A, COL_ACCENT = 0x4ADE80;
const uint32_t COL_TEXT = 0xF1F5F9, COL_SUB = 0x94A3B8, COL_DIM = 0x3B4654;

struct Overhead {
  UiState *st = nullptr;
  lv_obj_t *root = nullptr;
  lv_obj_t *glow = nullptr;
  lv_obj_t *plane = nullptr;
  lv_obj_t *card = nullptr;
  lv_obj_t *callsign = nullptr, *airline = nullptr, *type = nullptr, *alt = nullptr, *dist = nullptr;
  lv_obj_t *idle = nullptr;
  char shown_icao[7] = "";
  lv_coord_t px = -1, py = -1;
  int16_t angle = -1;
  uint32_t last_text_ms = 0;
};
Overhead g_oh;

const size_t PLANE_BYTES = (size_t)PLANE_PX * PLANE_PX * LV_IMG_PX_SIZE_ALPHA_BYTE;
lv_img_dsc_t g_body_dsc, g_glow_dsc;   // pixel data in PSRAM: internal RAM is kept for TLS

// ALPHA_8BIT `src` -> TRUE_COLOR_ALPHA in `color`.
bool expand(const lv_img_dsc_t &src, uint32_t color, lv_img_dsc_t &out) {
  uint8_t *map = static_cast<uint8_t *>(heap_caps_malloc(PLANE_BYTES, MALLOC_CAP_SPIRAM));
  if (!map) return false;
  lv_color_t c = lv_color_hex(color);
  for (uint32_t i = 0; i < (uint32_t)PLANE_PX * PLANE_PX; ++i) {
    uint8_t *px = &map[i * LV_IMG_PX_SIZE_ALPHA_BYTE];
    memcpy(px, &c, sizeof(lv_color_t));
    px[LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = src.data[i];
  }
  memset(&out, 0, sizeof(out));
  out.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  out.header.w = PLANE_PX;
  out.header.h = PLANE_PX;
  out.data_size = PLANE_BYTES;
  out.data = map;
  return true;
}

void glowOpaCb(void *obj, int32_t v) { lv_obj_set_style_img_opa(static_cast<lv_obj_t *>(obj), (lv_opa_t)v, 0); }

lv_obj_t *makePlaneImg(lv_obj_t *parent, const lv_img_dsc_t *dsc) {
  lv_obj_t *img = lv_img_create(parent);
  if (dsc->data) lv_img_set_src(img, dsc);
  lv_img_set_pivot(img, PLANE_PX / 2, PLANE_PX / 2);
  lv_img_set_antialias(img, true);
  lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
  return img;
}

void setTextIfChanged(lv_obj_t *label, const char *text) {
  const char *cur = lv_label_get_text(label);
  if (!cur || strcmp(cur, text) != 0) lv_label_set_text(label, text);
}

void setHidden(lv_obj_t *o, bool hidden) {
  if (hidden != lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
    if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_coord_t y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_width(l, CARD_W - 24);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, y);
  lv_label_set_text(l, "");
  return l;
}

void withCommas(char *out, size_t n, long v) {
  char d[16];
  snprintf(d, sizeof(d), "%ld", v < 0 ? -v : v);
  size_t len = strlen(d), o = 0;
  if (v < 0 && o + 1 < n) out[o++] = '-';
  for (size_t i = 0; i < len && o + 1 < n; ++i) {
    out[o++] = d[i];
    size_t left = len - i - 1;
    if (left > 0 && left % 3 == 0 && o + 1 < n) out[o++] = ',';
  }
  out[o] = '\0';
}

// Largest distance from (cx, cy) to any corner of the card placed at (x, y).
float farCorner(float x, float y, float cx, float cy) {
  float m = 0.f;
  for (int i = 0; i < 4; ++i) {
    float dx = x + ((i & 1) ? CARD_W : 0) - cx, dy = y + ((i & 2) ? CARD_H : 0) - cy;
    m = fmaxf(m, sqrtf(dx * dx + dy * dy));
  }
  return m;
}

void fillCard(Overhead &o, const Aircraft &a, float dist_m) {
  const AircraftReport &r = a.last;
  const MetadataRow *m = a.has_meta ? &a.meta : nullptr;
  char buf[64], num[16];
  setTextIfChanged(o.callsign, r.callsign[0] ? r.callsign : (m && m->reg[0] ? m->reg : r.icao24));
  airlineDisplayName(r.callsign, m, buf, sizeof(buf));
  setTextIfChanged(o.airline, buf);
  if (m && (m->typecode[0] || m->reg[0]))
    snprintf(buf, sizeof(buf), "%s%s%s", m->typecode, (m->typecode[0] && m->reg[0]) ? "  " LV_SYMBOL_BULLET "  " : "", m->reg);
  else
    snprintf(buf, sizeof(buf), "Type unknown");
  setTextIfChanged(o.type, buf);

  float alt_m = std::isnan(r.baro_alt_m) ? r.geo_alt_m : r.baro_alt_m;
  char alt[24] = "-", spd[16] = "-";
  if (!std::isnan(alt_m)) { withCommas(num, sizeof(num), lroundf(mToFt(alt_m))); snprintf(alt, sizeof(alt), "%s ft", num); }
  if (!std::isnan(r.velocity_mps)) snprintf(spd, sizeof(spd), "%ld kt", lroundf(mpsToKt(r.velocity_mps)));
  snprintf(buf, sizeof(buf), "%s  " LV_SYMBOL_BULLET "  %s", alt, spd);
  setTextIfChanged(o.alt, buf);

  snprintf(buf, sizeof(buf), "%.1f km from home", dist_m / 1000.f);
  setTextIfChanged(o.dist, buf);
}

}  // namespace

lv_obj_t *overheadCreate(lv_obj_t *parent, UiState *st) {
  Overhead &o = g_oh;
  o.st = st;
  if (!expand(plane_halo_body, COL_PLANE, g_body_dsc) || !expand(plane_halo_glow, COL_GLOW, g_glow_dsc))
    Serial.println("[overhead] PSRAM alloc for plane failed");

  o.root = lv_obj_create(parent);
  lv_obj_remove_style_all(o.root);
  lv_obj_set_size(o.root, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(o.root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(o.root, LV_OPA_COVER, 0);
  lv_obj_add_flag(o.root, LV_OBJ_FLAG_CLICKABLE);   // swallow taps: the radar underneath must not react
  lv_obj_clear_flag(o.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(o.root, LV_OBJ_FLAG_HIDDEN);

  o.glow = makePlaneImg(o.root, &g_glow_dsc);   // under the plane
  o.plane = makePlaneImg(o.root, &g_body_dsc);
  lv_anim_t a;   // the glow breathes slowly
  lv_anim_init(&a);
  lv_anim_set_var(&a, o.glow);
  lv_anim_set_exec_cb(&a, glowOpaCb);
  lv_anim_set_values(&a, GLOW_OPA_MIN, GLOW_OPA_MAX);
  lv_anim_set_time(&a, GLOW_PULSE_MS);
  lv_anim_set_playback_time(&a, GLOW_PULSE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);

  o.card = lv_obj_create(o.root);
  lv_obj_set_size(o.card, CARD_W, CARD_H);
  lv_obj_set_style_radius(o.card, 16, 0);
  lv_obj_set_style_bg_color(o.card, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_bg_opa(o.card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o.card, 1, 0);
  lv_obj_set_style_border_color(o.card, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_shadow_width(o.card, 0, 0);
  lv_obj_set_style_pad_all(o.card, 12, 0);
  lv_obj_clear_flag(o.card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(o.card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(o.card, LV_OBJ_FLAG_HIDDEN);
  o.callsign = makeLabel(o.card, &lv_font_montserrat_20, COL_TEXT, -2);
  o.airline = makeLabel(o.card, &lv_font_montserrat_14, COL_ACCENT, 22);
  o.type = makeLabel(o.card, &lv_font_montserrat_12, COL_SUB, 40);
  o.alt = makeLabel(o.card, &lv_font_montserrat_14, COL_TEXT, 56);
  o.dist = makeLabel(o.card, &lv_font_montserrat_10, COL_DIM, 75);
  lv_obj_set_style_text_color(o.dist, lv_color_hex(COL_SUB), 0);

  o.idle = lv_label_create(o.root);
  lv_obj_set_style_text_font(o.idle, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(o.idle, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_align(o.idle, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(o.idle, "");
  lv_obj_center(o.idle);
  return o.root;
}

void overheadOpen(lv_obj_t *root) {
  if (!root) return;
  g_oh.shown_icao[0] = '\0';
  g_oh.px = g_oh.py = -1;
  g_oh.angle = -1;
  g_oh.last_text_ms = 0;
  char idle[64];
  snprintf(idle, sizeof(idle), "Watching for planes\nwithin %.0f km", g_oh.st->overhead_radius_m / 1000.f);
  lv_label_set_text(g_oh.idle, idle);
  lv_obj_center(g_oh.idle);
  lv_obj_move_foreground(root);
  setHidden(root, false);
  Serial.println("[overhead] open");
  overheadTick(root, millis());
}

void overheadClose(lv_obj_t *root) {
  if (!root) return;
  setHidden(root, true);
  Serial.println("[overhead] close");
}

bool overheadIsOpen(lv_obj_t *root) { return root && !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN); }

void overheadTick(lv_obj_t *root, uint32_t now_ms) {
  if (!overheadIsOpen(root)) return;
  Overhead &o = g_oh;
  const UiState *st = o.st;
  float dist_m = -1.f;
  int i = st ? nearestAirborneWithin(st->snap, st->motion, st->snap_n, st->home, st->overhead_radius_m, &dist_m) : -1;
  if (i < 0) {
    if (o.shown_icao[0]) Serial.println("[overhead] clear");
    o.shown_icao[0] = '\0';
    setHidden(o.plane, true);
    setHidden(o.glow, true);
    setHidden(o.card, true);
    setHidden(o.idle, false);
    return;
  }
  const Aircraft &a = st->snap[i];
  const SpriteMotion &mo = st->motion[i];
  float lat = mo.initialised ? mo.cur_lat : a.last.lat;
  float lon = mo.initialised ? mo.cur_lon : a.last.lon;
  float track = mo.initialised ? mo.cur_track : (std::isnan(a.last.track_deg) ? 0.f : a.last.track_deg);

  // Home at the centre, the chosen range at the screen's edge (less a margin for the plane).
  float w = (float)lv_obj_get_width(root), h = (float)lv_obj_get_height(root);
  float cx = floorf(w / 2.f), cy = floorf(h / 2.f);
  float r_px = fminf(w, h) / 2.f - PLANE_HALF - 8.f;
  geo::LocalXY l = geo::toLocal(st->home, lat, lon);
  float s = r_px / st->overhead_radius_m;
  float x = cx + l.x * s, y = cy - l.y * s;

  bool changed = strncmp(o.shown_icao, a.last.icao24, sizeof(o.shown_icao)) != 0;
  if (changed) {
    strncpy(o.shown_icao, a.last.icao24, sizeof(o.shown_icao) - 1);
    o.shown_icao[sizeof(o.shown_icao) - 1] = '\0';
    Serial.printf("[overhead] %s %.1f km\n", a.last.icao24, dist_m / 1000.f);
  }

  lv_coord_t px = (lv_coord_t)lroundf(x - PLANE_PX / 2.f), py = (lv_coord_t)lroundf(y - PLANE_PX / 2.f);
  int16_t angle = (int16_t)(lroundf(track * 10.f) % 3600);
  bool moved = px != o.px || py != o.py;
  if (moved) {
    lv_obj_set_pos(o.glow, px, py);
    lv_obj_set_pos(o.plane, px, py);
    o.px = px;
    o.py = py;
  }
  if (angle != o.angle) {
    lv_img_set_angle(o.glow, angle);
    lv_img_set_angle(o.plane, angle);
    o.angle = angle;
  }

  // Card beside the plane, on the side towards the centre, then nudged inwards
  // until all four corners sit inside the round glass.
  if (moved || changed) {
    float card_x = (x <= cx) ? x + PLANE_HALF + CARD_GAP : x - PLANE_HALF - CARD_GAP - CARD_W;
    float card_y = y - CARD_H / 2.f;
    float limit = fminf(w, h) / 2.f - 4.f;
    for (int k = 0; k < 200 && farCorner(card_x, card_y, cx, cy) > limit; ++k) {
      float mx = card_x + CARD_W / 2.f - cx, my = card_y + CARD_H / 2.f - cy;
      float d = sqrtf(mx * mx + my * my);
      if (d < 1.f) break;
      card_x -= 3.f * mx / d;
      card_y -= 3.f * my / d;
    }
    lv_obj_set_pos(o.card, (lv_coord_t)lroundf(card_x), (lv_coord_t)lroundf(card_y));
  }

  if (changed || now_ms - o.last_text_ms >= 1000) {
    o.last_text_ms = now_ms;
    fillCard(o, a, dist_m);
  }
  setHidden(o.idle, true);
  setHidden(o.glow, false);
  setHidden(o.plane, false);
  setHidden(o.card, false);
}
