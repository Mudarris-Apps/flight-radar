#pragma once
#include <lvgl.h>
#include "ui_state.h"
void aircraftLayerCreate(lv_obj_t *parent, UiState *st, lv_obj_t *radar_view);
void aircraftLayerTick(uint32_t now_ms);                 // every UI_TICK_MS, LVGL task
// Index into st->snap of the nearest shown sprite within TAP_HIT_RADIUS_PX, or -1.
// dmin (optional) gets the distance in px to the nearest shown sprite at any range, or -1 if none is shown.
int  aircraftLayerHitTest(lv_coord_t x, lv_coord_t y, float *dmin = nullptr);
int  aircraftLayerVisibleCount();                        // sprites shown after the last tick (diagnostics)
int  aircraftLayerCreatedCount();                        // slots whose LVGL objects exist (diagnostics)
