#pragma once
#include "aircraft.h"
struct DisplayState { float lat, lon, track_deg; bool frozen; bool valid; };
DisplayState deadReckon(const AircraftReport &r, uint32_t now_s);
float easeFraction(uint32_t elapsed_ms, uint32_t duration_ms);   // smoothstep, clamped 0..1
float lerpAngleDeg(float from, float to, float t);               // shortest way, result in [0,360)
struct SpriteMotion {
  float cur_lat, cur_lon, cur_track;
  float from_lat, from_lon, from_track;
  uint32_t ease_start_ms; bool easing; bool initialised;
  uint32_t seen_time_position;
  bool frozen;
};
void spriteMotionUpdate(SpriteMotion &m, const AircraftReport &r, uint32_t now_s, uint32_t now_ms);
