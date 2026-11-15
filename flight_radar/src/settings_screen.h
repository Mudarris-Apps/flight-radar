// settings_screen.h: modal settings menu (trail length, Wi-Fi, overhead range and mode) driven by the knob (LVGL task only).
#pragma once
#include <lvgl.h>
#include "ui_state.h"
lv_obj_t *settingsScreenCreate(lv_obj_t *parent, UiState *st);
void      settingsScreenOpen(lv_obj_t *s);
bool      settingsScreenHandle(lv_obj_t *s, InputEvent e);   // true if consumed; PRESS acts on the highlighted row, LONG_PRESS closes
bool      settingsScreenIsOpen(lv_obj_t *s);
void      settingsScreenSetWifiCallback(void (*cb)());       // PRESS on the Wi-Fi row closes the panel and calls this
void      settingsScreenSetOverheadCallback(void (*cb)());   // PRESS on the overhead row closes the panel and calls this
uint32_t  settingsLoadTrailWindow();        // Preferences "radar"/"trail_s", default TRAIL_WINDOW_DEFAULT_S
void      settingsSaveTrailWindow(uint32_t s);
uint32_t  settingsLoadOverheadRange();      // Preferences "radar"/"oh_range_m" in metres, default OVERHEAD_RADIUS_M
void      settingsSaveOverheadRange(uint32_t m);
