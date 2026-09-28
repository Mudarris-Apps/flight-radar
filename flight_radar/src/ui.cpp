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
#include "knob_input.h"
#include "net_task.h"
#include "radar_view.h"
#include "status_bar.h"
#include "ui_state.h"

namespace {

const uint32_t DOUBLE_TAP_MS = 350;
const int32_t DRAG_THRESHOLD_PX = 3;

UiState st;
AircraftStore *g_store = nullptr;
lv_obj_t *g_rv = nullptr;
lv_obj_t *g_bar = nullptr;
char g_prev_icao[MAX_AIRCRAFT][7];   // icao24 last seen in each snapshot slot
bool g_was_animating = false;
uint32_t g_last_status_ms = 0;
uint32_t g_last_age_ms = 0;
const uint32_t TRAIL_AGE_INVALIDATE_MS = 5000;
uint32_t g_last_log_ms = 0;
uint32_t g_ticks = 0;
uint32_t g_log_ticks = 0;      // g_ticks at the previous log line
uint32_t g_layer_us_max = 0;   // slowest aircraftLayerTick since the previous log line
uint64_t g_layer_us_sum = 0;

// Touch gesture state.
bool g_dragging = false;
int32_t g_pend_x = 0, g_pend_y = 0;
uint32_t g_last_click_ms = 0;
bool g_have_click = false;

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
      applyDrag(g_pend_x, g_pend_y);
    }
  } else if (code == LV_EVENT_SHORT_CLICKED) {
    if (g_dragging) return;
    uint32_t now = lv_tick_get();
    if (g_have_click && lv_tick_elaps(g_last_click_ms) <= DOUBLE_TAP_MS) {
      g_have_click = false;
      st.pan_x_m = st.pan_y_m = 0.f;
      radarViewInvalidate(g_rv);
    } else {
      g_have_click = true;
      g_last_click_ms = now;
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
      default:   // PRESS / LONG_PRESS: Tasks 13 to 15
        break;
    }
  }
  if (rotated) {
    clampPan();
    radarViewInvalidate(g_rv);
  }
}

void refreshSnapshot() {
  uint32_t gen = g_store->generation();
  if (gen == st.snap_generation) return;
  st.snap_n = g_store->snapshot(st.snap, MAX_AIRCRAFT);
  st.snap_generation = gen;
  for (size_t i = 0; i < MAX_AIRCRAFT; ++i) {
    const char *icao = i < st.snap_n ? st.snap[i].last.icao24 : "";
    if (strncmp(g_prev_icao[i], icao, sizeof(g_prev_icao[i])) != 0) {
      memset(&st.motion[i], 0, sizeof(SpriteMotion));
      strncpy(g_prev_icao[i], icao, sizeof(g_prev_icao[i]) - 1);
      g_prev_icao[i][sizeof(g_prev_icao[i]) - 1] = '\0';
    }
  }
  radarViewInvalidate(g_rv);
}

void tickTimerCb(lv_timer_t *) {
  uint32_t now = millis();
  st.zoom.tick(now);
  bool anim = st.zoom.animating();
  if (anim) clampPan();   // keep home within 90 % of the shrinking view radius
  if (anim || g_was_animating) radarViewInvalidate(g_rv);   // includes the final settled frame
  g_was_animating = anim;
  refreshSnapshot();
  uint32_t t0 = micros();
  aircraftLayerTick(now);
  uint32_t layer_us = micros() - t0;
  g_layer_us_sum += layer_us;
  if (layer_us > g_layer_us_max) g_layer_us_max = layer_us;
  if (now - g_last_status_ms >= 1000) {
    g_last_status_ms = now;
    statusBarUpdate(g_bar);
  }
  if (now - g_last_age_ms >= TRAIL_AGE_INVALIDATE_MS) {
    g_last_age_ms = now;
    if (st.snap_n > 0) radarViewInvalidate(g_rv);   // trail fade follows the clock
  }
  ++g_ticks;
  if (now - g_last_log_ms >= 30000) {
    uint32_t dt = now - g_last_log_ms, dticks = g_ticks - g_log_ticks;
    g_last_log_ms = now;
    g_log_ticks = g_ticks;
    Serial.printf("[ui] ticks=%u view=%.0f m pan=(%.0f,%.0f) snap=%u gen=%u status=\"%s\"\n",
                  (unsigned)g_ticks, st.zoom.viewRadiusM(), st.pan_x_m, st.pan_y_m,
                  (unsigned)st.snap_n, (unsigned)st.snap_generation, lv_label_get_text(g_bar));
    Serial.printf("[ui] tick period %.1f ms, sprites=%d, layer us avg=%u max=%u, lvgl stack hwm=%u B\n",
                  dticks ? (float)dt / (float)dticks : 0.f, aircraftLayerVisibleCount(),
                  dticks ? (unsigned)(g_layer_us_sum / dticks) : 0u, (unsigned)g_layer_us_max,
                  (unsigned)uxTaskGetStackHighWaterMark(NULL));
    g_layer_us_sum = 0;
    g_layer_us_max = 0;
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
  lv_obj_add_event_cb(g_rv, touchCb, LV_EVENT_SHORT_CLICKED, nullptr);
  aircraftLayerCreate(scr, &st, g_rv);   // sprites above the scope, below the status bar
  g_bar = statusBarCreate(scr);

  lv_timer_create(inputTimerCb, INPUT_DRAIN_MS, &st);
  lv_timer_create(tickTimerCb, UI_TICK_MS, &st);

  Serial.printf("[ui] init: disp %dx%d, view radius %.0f m, snap %u B psram\n",
                (int)lv_disp_get_hor_res(NULL), (int)lv_disp_get_ver_res(NULL),
                st.zoom.viewRadiusM(), (unsigned)(MAX_AIRCRAFT * sizeof(Aircraft)));
}
