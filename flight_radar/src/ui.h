#pragma once
#include <lvgl.h>
#include "aircraft_store.h"
#include "ui_state.h"
void uiInit(AircraftStore *store);     // call inside lvgl_port_lock
UiState  *uiState();                  // the single UI state (LVGL task only)
lv_obj_t *uiRadarView();              // the radar view object, nullptr before uiInit
