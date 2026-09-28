// ui.cpp: builds the screen and runs the UI timers (LVGL task context).
#include "ui.h"
#include <Arduino.h>
#include <lvgl.h>
#include <cmath>
#include <cstring>
#include "esp_heap_caps.h"
#include "../secrets.h"
#include "aircraft_layer.h"
#include "config.h"
#include "detail_card.h"
#include "knob_input.h"
#include "net_task.h"
#include "radar_view.h"
#include "selection.h"
#include "status_bar.h"
#include "ui_state.h"

namespace {

const uint32_t DOUBLE_TAP_MS = 350;
const int32_t DRAG_THRESHOLD_PX = 3;

UiState st;
AircraftStore *g_store = nullptr;
lv_obj_t *g_rv = nullptr;
lv_obj_t *g_bar = nullptr;
lv_obj_t *g_card = nullptr;
char g_prev_icao[MAX_AIRCRAFT][7];   // icao24 last seen in each snapshot slot
bool g_was_animating = false;
uint32_t g_last_status_ms = 0;
uint32_t g_last_log_ms = 0;
uint32_t g_ticks = 0;

#if RADAR_DIAG
// Serial timing diagnostics, bucketed by what the UI is doing:
// 0 idle creep, 1 the post-poll ease (EASE_MS after a new snapshot), 2 zoom or drag.
uint32_t g_log_ticks = 0;
uint32_t g_layer_us_max = 0;
uint64_t g_layer_us_sum = 0;
uint32_t g_prev_tick_ms = 0;
uint32_t g_snap_change_ms = 0;
struct Bucket { uint32_t ticks, tick_ms, refr, refr_ms, full, full_ms; uint64_t px; };
Bucket g_bucket[3] = {};
const char *const BUCKET_NAME[3] = {"idle", "ease", "zoom/drag"};

int currentBucket(uint32_t now) {
  if (st.zoom.animating() || st.dragging) return 2;
  if (g_snap_change_ms && now - g_snap_change_ms <= EASE_MS + UI_TICK_MS) return 1;
  return 0;
}

void refrMonitorCb(lv_disp_drv_t *drv, uint32_t time_ms, uint32_t px) {
  Bucket &b = g_bucket[currentBucket(millis())];
  ++b.refr;
  b.refr_ms += time_ms;
  b.px += px;
  if (px >= (uint32_t)drv->hor_res * (uint32_t)drv->ver_res) {
    ++b.full;
    b.full_ms += time_ms;
  }
}

void diagTickStart(uint32_t now) {
  if (g_prev_tick_ms) {
    Bucket &b = g_bucket[currentBucket(now)];
    ++b.ticks;
    b.tick_ms += now - g_prev_tick_ms;
  }
  g_prev_tick_ms = now;
}

void diagLog(uint32_t dt) {
  uint32_t dticks = g_ticks - g_log_ticks;
  g_log_ticks = g_ticks;
  Serial.printf("[diag] tick period %.1f ms, sprites=%d/%d created, layer us avg=%u max=%u, lvgl stack hwm=%u B, heap=%u\n",
                dticks ? (float)dt / (float)dticks : 0.f, aircraftLayerVisibleCount(), aircraftLayerCreatedCount(),
                dticks ? (unsigned)(g_layer_us_sum / dticks) : 0u, (unsigned)g_layer_us_max,
                (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  for (int i = 0; i < 3; ++i) {
    Bucket &b = g_bucket[i];
    uint32_t pn = b.refr - b.full;
    Serial.printf("[diag] %-9s tick %.1f ms (n=%u); refresh n=%u avg %.1f ms %u px; full n=%u avg %.1f ms; partial avg %.1f ms\n",
                  BUCKET_NAME[i], b.ticks ? (float)b.tick_ms / b.ticks : 0.f, (unsigned)b.ticks, (unsigned)b.refr,
                  b.refr ? (float)b.refr_ms / b.refr : 0.f, b.refr ? (unsigned)(b.px / b.refr) : 0u, (unsigned)b.full,
                  b.full ? (float)b.full_ms / b.full : 0.f, pn ? (float)(b.refr_ms - b.full_ms) / pn : 0.f);
    b = Bucket{};
  }
  g_layer_us_sum = 0;
  g_layer_us_max = 0;
}
#endif

// Touch gesture state.
bool g_dragging = false;
int32_t g_pend_x = 0, g_pend_y = 0;
uint32_t g_last_click_ms = 0;
bool g_have_click = false;

// Selection: `i` indexes st.snap, -1 clears. Logs one line and redraws trails.
void setSelection(int i) {
  if (i >= 0 && (size_t)i < st.snap_n) {
    strncpy(st.selected_icao24, st.snap[i].last.icao24, sizeof(st.selected_icao24) - 1);
    st.selected_icao24[sizeof(st.selected_icao24) - 1] = '\0';
  } else {
    st.selected_icao24[0] = '\0';
    if (st.card_open) Serial.println("[card] close");
    st.card_open = false;
  }
  Serial.printf("[sel] %s\n", st.selected_icao24[0] ? st.selected_icao24 : "none");
  radarViewInvalidate(g_rv);
  detailCardRefresh(g_card);   // an open card follows the selection
}

// After a snapshot change: drop a selection whose aircraft left the snapshot.
void dropStaleSelection() {
  if (!st.selected_icao24[0]) return;
  for (size_t i = 0; i < st.snap_n; ++i)
    if (strncmp(st.snap[i].last.icao24, st.selected_icao24, sizeof(st.selected_icao24)) == 0) return;
  setSelection(-1);
}

void clampPan() {
  float lim = 0.9f * st.zoom.viewRadiusM();
  float d = hypotf(st.pan_x_m, st.pan_y_m);
  if (d > lim && d > 0.f) {
    st.pan_x_m *= lim / d;
    st.pan_y_m *= lim / d;
  }
}

void applyDrag(int32_t dx, int32_t dy) {
  geo::Projection P = radarViewProjection(g_rv);
  float s = geo::scalePxPerM(P);
  if (s <= 0.f) return;
  st.pan_x_m += (float)dx / s;
  st.pan_y_m -= (float)dy / s;
  clampPan();
  radarViewInvalidate(g_rv);
}

void touchCb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    g_dragging = false;
    g_pend_x = g_pend_y = 0;
  } else if (code == LV_EVENT_PRESSING) {
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t v;
    lv_indev_get_vect(indev, &v);
    if (v.x == 0 && v.y == 0) return;
    if (g_dragging) {
      applyDrag(v.x, v.y);
      return;
    }
    g_pend_x += v.x;
    g_pend_y += v.y;
    if (abs(g_pend_x) > DRAG_THRESHOLD_PX || abs(g_pend_y) > DRAG_THRESHOLD_PX) {
      g_dragging = true;
      st.dragging = true;   // radar_view skips the trail pass while dragging
      applyDrag(g_pend_x, g_pend_y);
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (st.dragging) {
      st.dragging = false;
      radarViewInvalidate(g_rv);   // settled frame with trails
    }
  } else if (code == LV_EVENT_SHORT_CLICKED) {
    if (g_dragging) return;
    uint32_t now = lv_tick_get();
    if (g_have_click && lv_tick_elaps(g_last_click_ms) <= DOUBLE_TAP_MS) {
      g_have_click = false;
      st.pan_x_m = st.pan_y_m = 0.f;
      radarViewInvalidate(g_rv);
    } else {
      // Single tap acts at once; a second tap within DOUBLE_TAP_MS still recentres.
      g_have_click = true;
      g_last_click_ms = now;
      lv_indev_t *indev = lv_indev_get_act();
      if (!indev) return;
      lv_point_t p;
      lv_indev_get_point(indev, &p);   // already rotated into logical coordinates
      setSelection(aircraftLayerHitTest(p.x, p.y));
    }
  }
}

void inputTimerCb(lv_timer_t *) {
  InputEvent ev;
  bool rotated = false;
  while (knobInputPop(ev)) {
    switch (ev) {
      case InputEvent::ROTATE_LEFT:
      case InputEvent::ROTATE_RIGHT:
        st.zoom.handleEvent(ev);
        rotated = true;
        break;
      case InputEvent::PRESS:
        setSelection(selectNextByDistance(st.snap, st.snap_n, st.home, st.selected_icao24));
        break;
      case InputEvent::LONG_PRESS:
        if (st.selected_icao24[0]) {   // without a selection: settings (Task 15)
          st.card_open = !st.card_open;
          Serial.println(st.card_open ? "[card] open" : "[card] close");
          detailCardRefresh(g_card);
        }
        break;
      default:
        break;
    }
  }
  if (rotated) {
    clampPan();
    radarViewInvalidate(g_rv);
  }
}

bool refreshSnapshot() {   // true when a new snapshot was taken
  uint32_t gen = g_store->generation();
  if (gen == st.snap_generation) return false;
  st.snap_n = g_store->snapshot(st.snap, MAX_AIRCRAFT);
  st.snap_generation = gen;
#if RADAR_DIAG
  g_snap_change_ms = millis();
#endif
  for (size_t i = 0; i < MAX_AIRCRAFT; ++i) {
    const char *icao = i < st.snap_n ? st.snap[i].last.icao24 : "";
    if (strncmp(g_prev_icao[i], icao, sizeof(g_prev_icao[i])) != 0) {
      memset(&st.motion[i], 0, sizeof(SpriteMotion));
      strncpy(g_prev_icao[i], icao, sizeof(g_prev_icao[i]) - 1);
      g_prev_icao[i][sizeof(g_prev_icao[i]) - 1] = '\0';
    }
  }
  radarViewInvalidate(g_rv);
  return true;
}

void tickTimerCb(lv_timer_t *) {
  uint32_t now = millis();
#if RADAR_DIAG
  diagTickStart(now);
#endif
  st.zoom.tick(now);
  bool anim = st.zoom.animating();
  if (anim) clampPan();   // keep home within 90 % of the shrinking view radius
  if (anim || g_was_animating) radarViewInvalidate(g_rv);   // includes the final settled frame
  g_was_animating = anim;
  if (refreshSnapshot()) dropStaleSelection();
#if RADAR_DIAG
  uint32_t t0 = micros();
#endif
  aircraftLayerTick(now);
#if RADAR_DIAG
  uint32_t layer_us = micros() - t0;
  g_layer_us_sum += layer_us;
  if (layer_us > g_layer_us_max) g_layer_us_max = layer_us;
#endif
  if (now - g_last_status_ms >= 1000) {
    g_last_status_ms = now;
    statusBarUpdate(g_bar);
    detailCardRefresh(g_card);
  }
  ++g_ticks;
  if (now - g_last_log_ms >= 30000) {
#if RADAR_DIAG
    uint32_t dt = now - g_last_log_ms;
#endif
    g_last_log_ms = now;
    Serial.printf("[ui] ticks=%u view=%.0f m pan=(%.0f,%.0f) snap=%u gen=%u status=\"%s\"\n",
                  (unsigned)g_ticks, st.zoom.viewRadiusM(), st.pan_x_m, st.pan_y_m,
                  (unsigned)st.snap_n, (unsigned)st.snap_generation, lv_label_get_text(g_bar));
#if RADAR_DIAG
    diagLog(dt);
#endif
  }
}

}  // namespace

UiState *uiState() { return &st; }
lv_obj_t *uiRadarView() { return g_rv; }

void uiInit(AircraftStore *store) {
  g_store = store;
  st.snap = static_cast<Aircraft *>(heap_caps_calloc(MAX_AIRCRAFT, sizeof(Aircraft), MALLOC_CAP_SPIRAM));
  st.motion = static_cast<SpriteMotion *>(heap_caps_calloc(MAX_AIRCRAFT, sizeof(SpriteMotion), MALLOC_CAP_SPIRAM));
  if (!st.snap || !st.motion) {
    Serial.println("[ui] PSRAM alloc for snapshot failed");
    return;
  }
  memset(g_prev_icao, 0, sizeof(g_prev_icao));
  st.home = geo::makeHome(HOME_LAT, HOME_LON);

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x05080C), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  g_rv = radarViewCreate(scr, &st);
  lv_obj_add_event_cb(g_rv, touchCb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(g_rv, touchCb, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(g_rv, touchCb, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(g_rv, touchCb, LV_EVENT_PRESS_LOST, nullptr);
  lv_obj_add_event_cb(g_rv, touchCb, LV_EVENT_SHORT_CLICKED, nullptr);
  aircraftLayerCreate(scr, &st, g_rv);   // sprites above the scope, below the status bar
  g_card = detailCardCreate(scr, &st);   // above the sprites, below the status bar
  g_bar = statusBarCreate(scr);

#if RADAR_DIAG
  lv_disp_t *disp = lv_disp_get_default();
  if (disp && disp->driver && !disp->driver->monitor_cb) disp->driver->monitor_cb = refrMonitorCb;
#endif

  lv_timer_create(inputTimerCb, INPUT_DRAIN_MS, &st);
  lv_timer_create(tickTimerCb, UI_TICK_MS, &st);

  Serial.printf("[ui] init: disp %dx%d, view radius %.0f m, snap %u B psram\n",
                (int)lv_disp_get_hor_res(NULL), (int)lv_disp_get_ver_res(NULL),
                st.zoom.viewRadiusM(), (unsigned)(MAX_AIRCRAFT * sizeof(Aircraft)));
}
