#include "dead_reckoning.h"
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kMPerDegLat = 110574.0f;
constexpr float kMPerDegLonEq = 111320.0f;
}  // namespace

DisplayState deadReckon(const AircraftReport &r, uint32_t now_s) {
  DisplayState d{};
  if (std::isnan(r.lat) || std::isnan(r.lon)) {
    d.valid = false;
    return d;
  }
  d.valid = true;
  d.track_deg = std::isnan(r.track_deg) ? 0.0f : r.track_deg;
  d.lat = r.lat;
  d.lon = r.lon;

  if (r.on_ground || std::isnan(r.velocity_mps) || std::isnan(r.track_deg)) {
    d.frozen = false;
    return d;
  }

  uint32_t dt = 0;
  bool frozen = false;
  if (r.time_position != 0 && now_s >= r.time_position) {
    uint32_t elapsed = now_s - r.time_position;
    uint32_t cap = static_cast<uint32_t>(DR_MAX_EXTRAPOLATION_S);
    dt = elapsed < cap ? elapsed : cap;
    frozen = elapsed > cap;
  }
  // else time_position == 0 (no fix time), or now_s < time_position: dt stays 0, frozen stays false.

  float track_rad = r.track_deg * kPi / 180.0f;
  float lat_rad = r.lat * kPi / 180.0f;
  float dist = r.velocity_mps * static_cast<float>(dt);
  d.lat = r.lat + dist * std::cos(track_rad) / kMPerDegLat;
  d.lon = r.lon + dist * std::sin(track_rad) / (kMPerDegLonEq * std::cos(lat_rad));
  d.frozen = frozen;
  return d;
}

float easeFraction(uint32_t elapsed_ms, uint32_t duration_ms) {
  if (duration_ms == 0) return 1.0f;
  float t = static_cast<float>(elapsed_ms) / static_cast<float>(duration_ms);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

float lerpAngleDeg(float from, float to, float t) {
  float diff = std::fmod(to - from, 360.0f);
  if (diff > 180.0f) diff -= 360.0f;
  else if (diff < -180.0f) diff += 360.0f;
  float result = from + diff * t;
  result = std::fmod(result, 360.0f);
  if (result < 0.0f) result += 360.0f;
  return result;
}

void spriteMotionUpdate(SpriteMotion &m, const AircraftReport &r, uint32_t now_s, uint32_t now_ms) {
  DisplayState dr = deadReckon(r, now_s);
  if (dr.valid) {
    if (!m.initialised) {
      m.cur_lat = dr.lat;
      m.cur_lon = dr.lon;
      m.cur_track = dr.track_deg;
      m.initialised = true;
      m.easing = false;
    } else {
      if (r.time_position != m.seen_time_position) {
        m.from_lat = m.cur_lat;
        m.from_lon = m.cur_lon;
        m.from_track = m.cur_track;
        m.ease_start_ms = now_ms;
        m.easing = true;
      }
      if (m.easing) {
        float t = easeFraction(now_ms - m.ease_start_ms, EASE_MS);
        m.cur_lat = m.from_lat + (dr.lat - m.from_lat) * t;
        m.cur_lon = m.from_lon + (dr.lon - m.from_lon) * t;
        m.cur_track = lerpAngleDeg(m.from_track, dr.track_deg, t);
        if (t >= 1.0f) m.easing = false;
      } else {
        m.cur_lat = dr.lat;
        m.cur_lon = dr.lon;
        m.cur_track = dr.track_deg;
      }
    }
  }
  // "keep cur_* unchanged and do not start an ease" when the report is invalid;
  // seen_time_position/frozen are still tracked unconditionally.
  m.seen_time_position = r.time_position;
  m.frozen = dr.frozen;
}
