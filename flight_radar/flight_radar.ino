#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "secrets.h"
#include "src/config.h"
#include "src/geo.h"
#include "src/aircraft_store.h"
#include "src/net_task.h"

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
  lvgl_port_lock(-1);
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x05080C), 0);
  lv_obj_t *l = lv_label_create(lv_scr_act());
  lv_label_set_text(l, "RADAR");
  lv_obj_set_style_text_color(l, lv_color_hex(0x2F8F5A), 0);
  lv_obj_center(l);
  lvgl_port_unlock();

  static AircraftStore store(geo::makeHome(HOME_LAT, HOME_LON), OBS_RADIUS_M);
  g_store = &store;
  netTaskStart(&store);
}

// Temporary status print for Task 9 verification; replaced by the UI later.
void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 5000) {
    last = millis();
    NetStatus st = netStatusGet();
    Serial.printf("[main] net state=%u http=%u rate=%d aircraft=%u next=%us epoch=%u store=%u\n",
                  (unsigned)st.state, (unsigned)st.http_code, st.rate_remaining,
                  (unsigned)st.aircraft_count, (unsigned)st.next_poll_in_s, (unsigned)netNowEpoch(),
                  (unsigned)g_store->count());
  }
  delay(50);
}
