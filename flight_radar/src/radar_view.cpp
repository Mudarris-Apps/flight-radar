// radar_view.cpp: the scope itself. A single full-screen lv_obj whose
// LV_EVENT_DRAW_MAIN handler paints rings, compass ticks, airports, trails and
// the home marker straight onto the draw context. Sprites (Task 12) are
// sibling objects created after this one, so they sit on top.
//
// The port renders in 20-line bands, so the draw handler runs once per band
// for every refresh. Trail projection is therefore cached per frame state
// (zoom, pan, snapshot generation, clock, window) and each band only draws
// what intersects its clip area.
#include "radar_view.h"
#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "esp_heap_caps.h"
#include "aircraft_store.h"
#include "airports.h"
#include "config.h"
#include "net_task.h"

namespace {

const uint32_t COL_BG = 0x05080C;
const uint32_t COL_RING = 0x1B3A2A;
const uint32_t COL_RING_LABEL = 0x3E7A55;
const uint32_t COL_SCOPE = 0x2F8F5A;
const uint32_t COL_AIRPORT = 0x5AA0FF;
const uint32_t COL_TRAIL = 0xE0A030;
const uint32_t COL_SELECTED = 0xFFFFFF;
const uint32_t COL_HOME = 0xFFFFFF;

const size_t MAX_AIRPORTS_DRAWN = 64;
const uint32_t TRAIL_AGE_STEP_S = 5;

struct CachedPt { lv_coord_t x, y; uint32_t t; };

struct TrailCache {
  bool valid;
  float view_r, pan_x, pan_y, cx, cy, r_px;
  uint32_t gen, now, window;
  size_t snap_n;
  uint16_t n[MAX_AIRCRAFT];
  lv_area_t box[MAX_AIRCRAFT];
  CachedPt pts[MAX_AIRCRAFT][TRAIL_CAPACITY];
};

TrailCache *g_cache = nullptr;

lv_coord_t toCoord(float v) {
  if (v > 30000.f) return 30000;
  if (v < -30000.f) return -30000;
  return (lv_coord_t)lroundf(v);
}

geo::Projection makeProjection(lv_obj_t *obj, const UiState *st) {
  lv_area_t c;
  lv_obj_get_coords(obj, &c);
  float w = (float)lv_area_get_width(&c), h = (float)lv_area_get_height(&c);
  geo::Projection P;
  P.cx = (float)c.x1 + floorf(w / 2.f);
  P.cy = (float)c.y1 + floorf(h / 2.f);
  P.r_px = floorf((w < h ? w : h) / 2.f) - SCOPE_MARGIN_PX;
  P.view_radius_m = st->zoom.viewRadiusM();
  P.pan_x_m = st->pan_x_m;
  P.pan_y_m = st->pan_y_m;
  return P;
}

// Spec: pick the spacing from {2,5,10,20,25,50} km so that 3 to 5 rings fit.
// Smallest spacing giving 3..5 rings (20 km at full range, then 10, 5, 2 as
// the knob zooms in); if none does, the largest spacing giving >= 3; else 2.
int ringSpacingKm(float view_radius_m) {
  static const int SPACINGS[] = {2, 5, 10, 20, 25, 50};
  float r_km = view_radius_m / 1000.f;
  for (int s : SPACINGS) {
    int k = (int)floorf(r_km / (float)s);
    if (k >= 3 && k <= 5) return s;
  }
  for (int i = (int)(sizeof(SPACINGS) / sizeof(SPACINGS[0])) - 1; i >= 0; --i) {
    if ((int)floorf(r_km / (float)SPACINGS[i]) >= 3) return SPACINGS[i];
  }
  return 2;
}

void drawCircle(lv_draw_ctx_t *ctx, float cx, float cy, float r, lv_coord_t width, uint32_t col) {
  if (r < 1.f) return;
  lv_draw_arc_dsc_t d;
  lv_draw_arc_dsc_init(&d);
  d.color = lv_color_hex(col);
  d.width = width;
  d.opa = LV_OPA_COVER;
  lv_point_t c = {toCoord(cx), toCoord(cy)};
  lv_draw_arc(ctx, &d, &c, (uint16_t)lroundf(r), 0, 360);
}

void drawText(lv_draw_ctx_t *ctx, const char *txt, lv_coord_t x1, lv_coord_t y1, lv_coord_t w,
              uint32_t col, lv_text_align_t align) {
  lv_area_t a = {x1, y1, (lv_coord_t)(x1 + w - 1), (lv_coord_t)(y1 + 16)};
  if (!_lv_area_is_on(&a, ctx->clip_area)) return;
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.font = &lv_font_montserrat_12;
  d.color = lv_color_hex(col);
  d.opa = LV_OPA_COVER;
  d.align = align;
  lv_draw_label(ctx, &d, &a, txt, NULL);
}

void drawRings(lv_draw_ctx_t *ctx, const geo::Projection &P) {
  float scale = geo::scalePxPerM(P);
  int s = ringSpacingKm(P.view_radius_m);
  char buf[12];
  for (int k = 1; (float)(k * s) * 1000.f <= P.view_radius_m; ++k) {
    float r = (float)(k * s) * 1000.f * scale;
    drawCircle(ctx, P.cx, P.cy, r, 1, COL_RING);
    float label_y = P.cy - r - 14.f;
    if (label_y < P.cy - P.r_px + 2.f) continue;   // would sit outside the scope edge
    snprintf(buf, sizeof(buf), "%d km", k * s);
    drawText(ctx, buf, toCoord(P.cx - 30.f), toCoord(label_y), 60, COL_RING_LABEL, LV_TEXT_ALIGN_CENTER);
  }
}

// Observation ring: OBS_RADIUS_M around home, so it follows pan. Skipped only
// when the circle cannot cross the scope disc (scope wholly inside the ring,
// or the ring wholly outside the scope).
void drawObsRing(lv_draw_ctx_t *ctx, const UiState *st, const geo::Projection &P) {
  float hx, hy;
  geo::project(st->home, P, st->home.lat, st->home.lon, hx, hy);
  float r_obs = OBS_RADIUS_M * geo::scalePxPerM(P);
  float d = hypotf(hx - P.cx, hy - P.cy);
  if (r_obs - d > P.r_px + 2.f) return;   // scope entirely inside the ring
  if (d - r_obs > P.r_px + 2.f) return;   // ring entirely outside the scope
  drawCircle(ctx, hx, hy, r_obs, 2, COL_SCOPE);
}

void drawCompass(lv_draw_ctx_t *ctx, const geo::Projection &P) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = lv_color_hex(COL_SCOPE);
  d.width = 1;
  d.opa = LV_OPA_COVER;
  for (int deg = 0; deg < 360; deg += 30) {
    float a = (float)deg * (float)M_PI / 180.f;
    float sx = sinf(a), cy = -cosf(a);
    lv_point_t p1 = {toCoord(P.cx + sx * (P.r_px - 8.f)), toCoord(P.cy + cy * (P.r_px - 8.f))};
    lv_point_t p2 = {toCoord(P.cx + sx * (P.r_px - 2.f)), toCoord(P.cy + cy * (P.r_px - 2.f))};
    lv_draw_line(ctx, &d, &p1, &p2);
  }
  drawText(ctx, "N", toCoord(P.cx - 5.f), toCoord(P.cy - P.r_px + 8.f), 12, COL_SCOPE, LV_TEXT_ALIGN_LEFT);
}

void drawAirports(lv_draw_ctx_t *ctx, const UiState *st, const geo::Projection &P) {
  // The on-screen centre is at local (-pan_x, -pan_y) metres from home.
  float c_lat = st->home.lat + (-P.pan_y_m) / 110574.0f;
  float c_lon = st->home.lon + (-P.pan_x_m) / (111320.0f * st->home.cos_lat);
  geo::BBox b = geo::bbox(geo::makeHome(c_lat, c_lon), P.view_radius_m * 1.2f);
  const Airport *list[MAX_AIRPORTS_DRAWN];
  size_t n = airportsInBox(b.lamin, b.lamax, b.lomin, b.lomax, list, MAX_AIRPORTS_DRAWN);
  bool labels = P.view_radius_m <= AIRPORT_LABEL_RADIUS_M;

  lv_draw_rect_dsc_t rd;
  lv_draw_rect_dsc_init(&rd);
  rd.bg_opa = LV_OPA_TRANSP;
  rd.border_width = 1;
  rd.border_color = lv_color_hex(COL_AIRPORT);
  rd.border_opa = LV_OPA_COVER;
  rd.radius = 0;

  for (size_t i = 0; i < n; ++i) {
    float px, py;
    geo::project(st->home, P, list[i]->lat, list[i]->lon, px, py);
    if (!geo::insideScope(P, px, py)) continue;
    lv_coord_t x = toCoord(px), y = toCoord(py);
    lv_area_t sq = {(lv_coord_t)(x - 2), (lv_coord_t)(y - 2), (lv_coord_t)(x + 2), (lv_coord_t)(y + 2)};
    if (_lv_area_is_on(&sq, ctx->clip_area)) lv_draw_rect(ctx, &rd, &sq);
    if (labels) drawText(ctx, list[i]->ident, (lv_coord_t)(x + 6), (lv_coord_t)(y - 6), 40, COL_AIRPORT, LV_TEXT_ALIGN_LEFT);
  }
}

void refreshTrailCache(const UiState *st, const geo::Projection &P, uint32_t now) {
  TrailCache &c = *g_cache;
  if (c.valid && c.view_r == P.view_radius_m && c.pan_x == P.pan_x_m && c.pan_y == P.pan_y_m &&
      c.cx == P.cx && c.cy == P.cy && c.r_px == P.r_px && c.gen == st->snap_generation &&
      c.now == now && c.window == st->trail_window_s && c.snap_n == st->snap_n) {
    return;
  }
  static TrailPoint tp[TRAIL_CAPACITY];   // LVGL task only; keeps the 6 KB stack free
  c.valid = true;
  c.view_r = P.view_radius_m; c.pan_x = P.pan_x_m; c.pan_y = P.pan_y_m;
  c.cx = P.cx; c.cy = P.cy; c.r_px = P.r_px;
  c.gen = st->snap_generation; c.now = now; c.window = st->trail_window_s; c.snap_n = st->snap_n;
  size_t count = st->snap_n < MAX_AIRCRAFT ? st->snap_n : MAX_AIRCRAFT;
  for (size_t i = 0; i < count; ++i) {
    c.n[i] = 0;
    const Aircraft &a = st->snap[i];
    if (!a.in_use || now == 0) continue;
    size_t n = AircraftStore::trailWindow(a, now, st->trail_window_s, tp, TRAIL_CAPACITY);
    lv_area_t &box = c.box[i];
    box = {30000, 30000, -30000, -30000};
    for (size_t j = 0; j < n; ++j) {
      float px, py;
      geo::project(st->home, P, tp[j].lat, tp[j].lon, px, py);
      CachedPt &q = c.pts[i][j];
      q.x = toCoord(px); q.y = toCoord(py); q.t = tp[j].t;
      if (q.x < box.x1) box.x1 = q.x;
      if (q.y < box.y1) box.y1 = q.y;
      if (q.x > box.x2) box.x2 = q.x;
      if (q.y > box.y2) box.y2 = q.y;
    }
    box.x1 -= 3; box.y1 -= 3; box.x2 += 3; box.y2 += 3;
    c.n[i] = (uint16_t)n;
  }
}

bool ptInside(const geo::Projection &P, const CachedPt &q) {
  return geo::insideScope(P, (float)q.x, (float)q.y);
}

void drawTrails(lv_draw_ctx_t *ctx, const UiState *st, const geo::Projection &P) {
  if (!g_cache || !st->snap || st->snap_n == 0) return;
  // Trails age in 5 s steps: the cache key and the fade both use this rounded clock.
  uint32_t now = netNowEpoch();
  now -= now % TRAIL_AGE_STEP_S;
  refreshTrailCache(st, P, now);
  const TrailCache &c = *g_cache;
  float window = (float)(st->trail_window_s > 0 ? st->trail_window_s : 1);
  float lower = (float)now - window;
  size_t count = st->snap_n < MAX_AIRCRAFT ? st->snap_n : MAX_AIRCRAFT;

  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  for (size_t i = 0; i < count; ++i) {
    uint16_t n = c.n[i];
    if (n < 2) continue;
    const Aircraft &a = st->snap[i];
    bool selected = st->selected_icao24[0] && strcmp(a.last.icao24, st->selected_icao24) == 0;
    d.color = lv_color_hex(selected ? COL_SELECTED : COL_TRAIL);
    d.width = selected ? 3 : 2;
    const CachedPt *pts = c.pts[i];

    if (_lv_area_is_on(&c.box[i], ctx->clip_area)) {
      for (uint16_t j = 0; j + 1 < n; ++j) {
        const CachedPt &p = pts[j], &q = pts[j + 1];
        if (!ptInside(P, p) && !ptInside(P, q)) continue;
        lv_area_t seg = {LV_MIN(p.x, q.x), LV_MIN(p.y, q.y), LV_MAX(p.x, q.x), LV_MAX(p.y, q.y)};
        seg.x1 -= 3; seg.y1 -= 3; seg.x2 += 3; seg.y2 += 3;
        if (!_lv_area_is_on(&seg, ctx->clip_area)) continue;
        float o = 51.f + 204.f * ((float)q.t - lower) / window;
        if (o < 51.f) o = 51.f;
        if (o > 255.f) o = 255.f;
        d.opa = (lv_opa_t)o;
        lv_point_t p1 = {p.x, p.y}, p2 = {q.x, q.y};
        lv_draw_line(ctx, &d, &p1, &p2);
      }
    }
    // The leader (newest trail point to the eased sprite) is an lv_line in aircraft_layer,
    // so sprite motion never invalidates the scope.
  }
}

void drawHome(lv_draw_ctx_t *ctx, const UiState *st, const geo::Projection &P) {
  float hx, hy;
  geo::project(st->home, P, st->home.lat, st->home.lon, hx, hy);
  if (!geo::insideScope(P, hx, hy)) return;
  drawCircle(ctx, hx, hy, 3.f, 3, COL_HOME);   // filled 6 px dot (arc width == radius)
  drawCircle(ctx, hx, hy, 5.f, 1, COL_SCOPE);
}

void drawCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  UiState *st = static_cast<UiState *>(lv_event_get_user_data(e));
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  if (!st || !ctx) return;
  geo::Projection P = makeProjection(obj, st);
  drawRings(ctx, P);
  drawObsRing(ctx, st, P);
  drawCompass(ctx, P);
  drawAirports(ctx, st, P);
  // Trails are the expensive pass: skip them while zooming or dragging; the
  // settled frame (ui.cpp invalidates once after either ends) draws them again.
  if (!st->zoom.animating() && !st->dragging) drawTrails(ctx, st, P);
  drawHome(ctx, st, P);
}

}  // namespace

lv_obj_t *radarViewCreate(lv_obj_t *parent, UiState *st) {
  if (!g_cache) {
    g_cache = static_cast<TrailCache *>(heap_caps_calloc(1, sizeof(TrailCache), MALLOC_CAP_SPIRAM));
    if (!g_cache) Serial.println("[ui] radar: trail cache alloc failed, trails disabled");
  }
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, 0, 0);
  lv_obj_set_size(obj, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
  lv_obj_set_style_bg_color(obj, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_user_data(obj, st);
  lv_obj_add_event_cb(obj, drawCb, LV_EVENT_DRAW_MAIN, st);
  return obj;
}

void radarViewInvalidate(lv_obj_t *rv) {
  if (rv) lv_obj_invalidate(rv);
}

geo::Projection radarViewProjection(lv_obj_t *rv) {
  const UiState *st = static_cast<const UiState *>(lv_obj_get_user_data(rv));
  return makeProjection(rv, st);
}
