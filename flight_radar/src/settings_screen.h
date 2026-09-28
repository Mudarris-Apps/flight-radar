// settings_screen.h: modal trail-length picker driven by the knob (LVGL task only).
#pragma once
#include <lvgl.h>
#include "ui_state.h"
lv_obj_t *settingsScreenCreate(lv_obj_t *parent, UiState *st);
void      settingsScreenOpen(lv_obj_t *s);
bool      settingsScreenHandle(lv_obj_t *s, InputEvent e);   // true if consumed; PRESS commits st->trail_window_s and closes, LONG_PRESS cancels and closes
bool      settingsScreenIsOpen(lv_obj_t *s);
uint32_t  settingsLoadTrailWindow();        // Preferences "radar"/"trail_s", default TRAIL_WINDOW_DEFAULT_S
void      settingsSaveTrailWindow(uint32_t s);
