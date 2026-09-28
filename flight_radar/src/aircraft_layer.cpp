// aircraft_layer.cpp: one lv_img airplane sprite (plus a callsign label) per
// snapshot slot, placed at the dead-reckoned, eased position in
// st->motion[i]. The scope (radar_view) draws the trails and a leader segment
// ending at the same cur_lat/cur_lon, so the sprite sits on the end of it.
//
// LVGL invalidates on every setter call, so each slot caches what it last
// applied and only calls a setter when the value changed.
//
// LVGL 8.4 cannot rotate LV_IMG_CF_ALPHA_8BIT images: with an angle set it
// falls back to a line-by-line path that transforms each row on its own. The
// A8 plane_24 is therefore expanded once into a TRUE_COLOR_ALPHA copy (white,
// same alpha), which rotates correctly and is tinted by img_recolor.
#include "aircraft_layer.h"
#include <Arduino.h>
#include <cmath>
#include <cstring>
#include "config.h"
#include "geo.h"
#include "net_task.h"
#include "radar_view.h"
#include "sprites/plane_24.h"

namespace {

const uint32_t COL_NORMAL = 0xF2B632;
const uint32_t COL_STALE = 0x7A6A40;
const uint32_t COL_GROUND = 0x6F7A85;
const uint32_t COL_SELECTED = 0xFFFFFF;
const uint32_t COL_LABEL = 0xF2B632;
const lv_coord_t SPRITE_HALF = 12;
const lv_coord_t RING_SIZE = 34;
const lv_coord_t NO_POS = INT16_MIN;
const int LEADER_PAD_PX = 3;

struct Slot {
  lv_obj_t *img;
  lv_obj_t *label;
  lv_coord_t x, y;        // sprite centre last applied (NO_POS if never)
  int16_t angle;          // -1 if never
  uint32_t colour;        // 0xFFFFFFFF if never
  bool img_shown;
  bool label_shown;
  lv_coord_t lx, ly;      // label position last applied
  char text[9];
};

UiState *g_st = nullptr;
lv_obj_t *g_rv = nullptr;
lv_obj_t *g_ring = nullptr;
Slot *g_slots = nullptr;
int g_selected_slot = -1;     // slot whose img was last raised above the ring
bool g_ring_shown = false;
lv_coord_t g_ring_x = NO_POS, g_ring_y = NO_POS;
int g_visible = 0;

uint8_t g_plane_argb_map[24 * 24 * LV_IMG_PX_SIZE_ALPHA_BYTE];
lv_img_dsc_t g_plane_argb;

void buildPlaneArgb() {
  const uint32_t w = plane_24.header.w, h = plane_24.header.h;
  lv_color_t white = lv_color_white();
  for (uint32_t i = 0; i < w * h; ++i) {
    uint8_t *px = &g_plane_argb_map[i * LV_IMG_PX_SIZE_ALPHA_BYTE];
    memcpy(px, &white, sizeof(lv_color_t));
    px[LV_IMG_PX_SIZE_ALPHA_BYTE - 1] = plane_24.data[i];
  }
  memset(&g_plane_argb, 0, sizeof(g_plane_argb));
  g_plane_argb.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  g_plane_argb.header.w = w;
  g_plane_argb.header.h = h;
  g_plane_argb.data_size = sizeof(g_plane_argb_map);
  g_plane_argb.data = g_plane_argb_map;
}

lv_coord_t toCoord(float v) {
  if (v > 30000.f) return 30000;
  if (v < -30000.f) return -30000;
  return (lv_coord_t)lroundf(v);
}

void setShown(lv_obj_t *o, bool &cached, bool shown) {
  if (cached == shown) return;
  cached = shown;
  if (shown) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// The scope's leader runs from the newest trail point to the sprite. When the
// sprite moves, redraw the scope under the old and new leader so it follows.
void invalidateLeader(const Aircraft &a, const geo::Projection &P, lv_coord_t old_x, lv_coord_t old_y,
                      lv_coord_t new_x, lv_coord_t new_y) {
  if (a.trail_len == 0) return;
  const TrailPoint &tp = a.trail[(a.trail_head + TRAIL_CAPACITY - 1) % TRAIL_CAPACITY];
  float sx, sy;
  geo::project(g_st->home, P, tp.lat, tp.lon, sx, sy);
  lv_coord_t x = toCoord(sx), y = toCoord(sy);
  lv_area_t ar = {LV_MIN(x, new_x), LV_MIN(y, new_y), LV_MAX(x, new_x), LV_MAX(y, new_y)};
  if (old_x != NO_POS) {
    ar.x1 = LV_MIN(ar.x1, old_x); ar.y1 = LV_MIN(ar.y1, old_y);
    ar.x2 = LV_MAX(ar.x2, old_x); ar.y2 = LV_MAX(ar.y2, old_y);
  }
  ar.x1 -= LEADER_PAD_PX; ar.y1 -= LEADER_PAD_PX; ar.x2 += LEADER_PAD_PX; ar.y2 += LEADER_PAD_PX;
  lv_obj_invalidate_area(g_rv, &ar);
}

void hideSlot(Slot &s) {
  setShown(s.img, s.img_shown, false);
  setShown(s.label, s.label_shown, false);
}

uint32_t colourFor(const Aircraft &a, const SpriteMotion &m, bool selected) {
  if (selected) return COL_SELECTED;
  if (a.last.on_ground) return COL_GROUND;
  if (a.stale || m.frozen) return COL_STALE;
  return COL_NORMAL;
}

}  // namespace

void aircraftLayerCreate(lv_obj_t *parent, UiState *st, lv_obj_t *radar_view) {
  g_st = st;
  g_rv = radar_view;
  if (!g_slots) g_slots = static_cast<Slot *>(calloc(MAX_AIRCRAFT, sizeof(Slot)));
  if (!g_slots) {
    Serial.println("[layer] slot alloc failed");
    return;
  }
  buildPlaneArgb();

  for (size_t i = 0; i < MAX_AIRCRAFT; ++i) {
    Slot &s = g_slots[i];
    s.img = lv_img_create(parent);
    lv_img_set_src(s.img, &g_plane_argb);
    lv_img_set_pivot(s.img, SPRITE_HALF, SPRITE_HALF);
    lv_img_set_antialias(s.img, true);
    lv_obj_set_style_img_recolor(s.img, lv_color_hex(COL_NORMAL), 0);
    lv_obj_set_style_img_recolor_opa(s.img, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s.img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s.img, LV_OBJ_FLAG_HIDDEN);
    s.x = s.y = NO_POS;
    s.lx = s.ly = NO_POS;
    s.angle = -1;
    s.colour = COL_NORMAL;
    s.img_shown = false;
  }
  for (size_t i = 0; i < MAX_AIRCRAFT; ++i) {
    Slot &s = g_slots[i];
    s.label = lv_label_create(parent);
    lv_obj_set_style_text_font(s.label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s.label, lv_color_hex(COL_LABEL), 0);
    lv_obj_clear_flag(s.label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(s.label, "");
    lv_obj_add_flag(s.label, LV_OBJ_FLAG_HIDDEN);
    s.text[0] = '\0';
    s.label_shown = false;
  }

  g_ring = lv_arc_create(parent);
  lv_obj_remove_style_all(g_ring);
  lv_obj_set_size(g_ring, RING_SIZE, RING_SIZE);
  lv_arc_set_bg_angles(g_ring, 0, 360);
  lv_arc_set_value(g_ring, 0);
  lv_obj_set_style_arc_width(g_ring, 2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(g_ring, lv_color_hex(COL_SELECTED), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(g_ring, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(g_ring, false, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(g_ring, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(g_ring, 0, LV_PART_INDICATOR);
  lv_obj_clear_flag(g_ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_ring, LV_OBJ_FLAG_HIDDEN);
  g_ring_shown = false;
}

void aircraftLayerTick(uint32_t now_ms) {
  if (!g_st || !g_slots || !g_rv || !g_st->snap || !g_st->motion) return;
  const uint32_t now_s = netNowEpoch();
  const geo::Projection P = radarViewProjection(g_rv);
  const bool zoom_labels = P.view_radius_m <= LABEL_VIEW_RADIUS_M;
  const bool any_selected = g_st->selected_icao24[0] != '\0';
  const size_t n = g_st->snap_n < MAX_AIRCRAFT ? g_st->snap_n : MAX_AIRCRAFT;
  int sel_slot = -1;
  lv_coord_t sel_x = 0, sel_y = 0;
  int visible = 0;

  for (size_t i = 0; i < n; ++i) {
    Slot &s = g_slots[i];
    const Aircraft &a = g_st->snap[i];
    SpriteMotion &m = g_st->motion[i];
    spriteMotionUpdate(m, a.last, now_s, now_ms);
    if (!m.initialised) {
      hideSlot(s);
      continue;
    }
    float fx, fy;
    geo::project(g_st->home, P, m.cur_lat, m.cur_lon, fx, fy);
    if (!geo::insideScope(P, fx, fy)) {
      hideSlot(s);
      continue;
    }
    const lv_coord_t px = toCoord(fx), py = toCoord(fy);
    const bool selected = any_selected && strcmp(a.last.icao24, g_st->selected_icao24) == 0;
    ++visible;

    if (px != s.x || py != s.y) {
      invalidateLeader(a, P, s.x, s.y, px, py);
      lv_obj_set_pos(s.img, px - SPRITE_HALF, py - SPRITE_HALF);
      s.x = px;
      s.y = py;
    }
    const int16_t angle = (int16_t)((((int)(m.cur_track * 10.f)) % 3600 + 3600) % 3600);
    if (angle != s.angle) {
      lv_img_set_angle(s.img, angle);
      s.angle = angle;
    }
    const uint32_t colour = colourFor(a, m, selected);
    if (colour != s.colour) {
      lv_obj_set_style_img_recolor(s.img, lv_color_hex(colour), 0);
      s.colour = colour;
    }
    setShown(s.img, s.img_shown, true);

    if (zoom_labels || selected) {
      const char *text = a.last.callsign[0] ? a.last.callsign : a.last.icao24;
      if (strncmp(text, s.text, sizeof(s.text)) != 0) {
        strncpy(s.text, text, sizeof(s.text) - 1);
        s.text[sizeof(s.text) - 1] = '\0';
        lv_label_set_text(s.label, s.text);
      }
      const lv_coord_t lx = px + 14, ly = py - 6;
      if (lx != s.lx || ly != s.ly) {
        lv_obj_set_pos(s.label, lx, ly);
        s.lx = lx;
        s.ly = ly;
      }
      setShown(s.label, s.label_shown, true);
    } else {
      setShown(s.label, s.label_shown, false);
    }

    if (selected) {
      sel_slot = (int)i;
      sel_x = px;
      sel_y = py;
    }
  }
  for (size_t i = n; i < MAX_AIRCRAFT; ++i) hideSlot(g_slots[i]);
  g_visible = visible;

  if (sel_slot >= 0) {
    if (sel_x != g_ring_x || sel_y != g_ring_y) {
      lv_obj_set_pos(g_ring, sel_x - RING_SIZE / 2, sel_y - RING_SIZE / 2);
      g_ring_x = sel_x;
      g_ring_y = sel_y;
    }
    setShown(g_ring, g_ring_shown, true);
    if (sel_slot != g_selected_slot) {
      // Raise the selected sprite above the other sprites, labels and the ring,
      // but keep it below the status bar (created after this layer).
      lv_obj_move_to_index(g_slots[sel_slot].img, lv_obj_get_index(g_ring));
      g_selected_slot = sel_slot;
    }
  } else {
    setShown(g_ring, g_ring_shown, false);
    g_selected_slot = -1;
  }
}

int aircraftLayerHitTest(lv_coord_t x, lv_coord_t y) {
  if (!g_st || !g_slots) return -1;
  const size_t n = g_st->snap_n < MAX_AIRCRAFT ? g_st->snap_n : MAX_AIRCRAFT;
  const int32_t r2 = (int32_t)TAP_HIT_RADIUS_PX * TAP_HIT_RADIUS_PX;
  int best = -1;
  int32_t best_d2 = r2 + 1;
  for (size_t i = 0; i < n; ++i) {
    const Slot &s = g_slots[i];
    if (!s.img_shown) continue;
    const int32_t dx = (int32_t)s.x - x, dy = (int32_t)s.y - y;
    const int32_t d2 = dx * dx + dy * dy;
    if (d2 <= r2 && d2 < best_d2) {
      best = (int)i;
      best_d2 = d2;
    }
  }
  return best;
}

int aircraftLayerVisibleCount() { return g_visible; }
