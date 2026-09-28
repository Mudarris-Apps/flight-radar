#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "secrets.h"
#include "src/config.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

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
}

void loop() { delay(1000); }
