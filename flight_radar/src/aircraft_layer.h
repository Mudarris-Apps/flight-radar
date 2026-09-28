#pragma once
#include <lvgl.h>
#include "ui_state.h"
void aircraftLayerCreate(lv_obj_t *parent, UiState *st, lv_obj_t *radar_view);
void aircraftLayerTick(uint32_t now_ms);                 // every UI_TICK_MS, LVGL task
int  aircraftLayerHitTest(lv_coord_t x, lv_coord_t y);   // index into st->snap or -1, within TAP_HIT_RADIUS_PX
int  aircraftLayerVisibleCount();                        // sprites shown after the last tick (diagnostics)
