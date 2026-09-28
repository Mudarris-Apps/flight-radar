#pragma once
#include "aircraft.h"
#include "geo.h"
#include "zoom_controller.h"
#include "dead_reckoning.h"
struct UiState {
  geo::Home home;
  ZoomController zoom;
  float pan_x_m = 0, pan_y_m = 0;
  char selected_icao24[7] = {0};
  uint32_t trail_window_s = TRAIL_WINDOW_DEFAULT_S;
  Aircraft *snap = nullptr;          // PSRAM array MAX_AIRCRAFT
  SpriteMotion *motion = nullptr;    // parallel to snap
  size_t snap_n = 0;
  uint32_t snap_generation = 0;
  bool card_open = false;
  bool settings_open = false;
  bool dragging = false;             // a pan drag is in progress (radar_view skips trails)
};
