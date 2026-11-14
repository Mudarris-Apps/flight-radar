#pragma once
#include <lvgl.h>
#include "ui_state.h"
lv_obj_t *radarViewCreate(lv_obj_t *parent, UiState *st);   // full-screen object; reads st on every draw
void      radarViewInvalidate(lv_obj_t *rv);
geo::Projection radarViewProjection(lv_obj_t *rv);           // current cx, cy, r_px, view radius, pan
void radarViewInvalidateTrail(lv_obj_t *rv, const char *icao24);   // repaint only that aircraft's trail box (selection colour change)
