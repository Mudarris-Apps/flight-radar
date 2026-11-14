// wifi_screen.h: full-screen Wi-Fi setup: a scanned network list and an on-screen
// keyboard, driven by the knob (turn = move, press = choose, hold = back) or by touch.
// LVGL task only.
#pragma once
#include <lvgl.h>
#include "zoom_controller.h"
lv_obj_t *wifiScreenCreate(lv_obj_t *parent);
void      wifiScreenOpen(lv_obj_t *w);
bool      wifiScreenIsOpen(lv_obj_t *w);
void      wifiScreenHandle(lv_obj_t *w, InputEvent e);
// Call every UI tick: follows scan results and join progress, and opens the screen
// by itself when no known network can be joined (so it never shows at home).
void      wifiScreenTick(lv_obj_t *w, uint32_t now_ms);
