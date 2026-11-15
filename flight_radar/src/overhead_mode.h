// overhead_mode.h: full-screen black view that shows only the nearest airborne
// aircraft within the chosen range (UiState::overhead_radius_m) of home, with its flight card beside it
// (LVGL task only). Opened from settings; a long knob press leaves it.
#pragma once
#include <lvgl.h>
#include "ui_state.h"
lv_obj_t *overheadCreate(lv_obj_t *parent, UiState *st);
void      overheadOpen(lv_obj_t *o);
void      overheadClose(lv_obj_t *o);
bool      overheadIsOpen(lv_obj_t *o);
void      overheadTick(lv_obj_t *o, uint32_t now_ms);   // every UI tick while open: moves the plane, refreshes the card
