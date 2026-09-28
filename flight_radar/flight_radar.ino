#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "secrets.h"
#include "src/config.h"
#include "src/geo.h"
#include "src/aircraft_store.h"
#include "src/net_task.h"
#include "src/knob_input.h"
#include "src/ui.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static AircraftStore *g_store = nullptr;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("[boot] flight_radar, reset reason %d\n", (int)esp_reset_reason());
  Board *board = new Board();
  board->init();
  assert(board->begin());
  lvgl_port_init(board->getLCD(), board->getTouch());

  static AircraftStore store(geo::makeHome(HOME_LAT, HOME_LON), OBS_RADIUS_M);
  g_store = &store;
  knobInputStart();

  lvgl_port_lock(-1);
  // The SH8601 driver offers mirror-X only (no mirror-Y / swap-XY), so the port
  // runs with sw_rotate and 180 degrees is done by LVGL. LVGL's indev also
  // rotates touch points for ROT_180, so the touch driver is left unmirrored.
#if DISPLAY_ROTATION_DEG == 180
  lv_disp_set_rotation(lv_disp_get_default(), LV_DISP_ROT_180);
#elif DISPLAY_ROTATION_DEG != 0
#error "DISPLAY_ROTATION_DEG must be 0 or 180"
#endif
  uiInit(&store);
  lvgl_port_unlock();

  netTaskStart(&store);
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 30000) {
    last = millis();
    NetStatus st = netStatusGet();
    Serial.printf("[main] net state=%u http=%u rate=%d aircraft=%u next=%us epoch=%u store=%u\n",
                  (unsigned)st.state, (unsigned)st.http_code, st.rate_remaining,
                  (unsigned)st.aircraft_count, (unsigned)st.next_poll_in_s, (unsigned)netNowEpoch(),
                  (unsigned)g_store->count());
  }
  delay(200);
}
