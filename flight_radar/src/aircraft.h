#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config.h"

struct AircraftReport {
  char icao24[7];
  char callsign[9];
  uint32_t time_position;   // 0 if null
  uint32_t last_contact;
  float lat, lon;           // NAN if null
  float baro_alt_m, geo_alt_m;
  bool on_ground;
  float velocity_mps, track_deg, vertical_rate_mps;
  char squawk[5];
  uint8_t category;
};

struct TrailPoint { float lat, lon; uint32_t t; };

struct MetadataRow;  // defined in metadata_table.h

struct Aircraft {
  AircraftReport last;
  uint32_t first_seen;
  uint32_t last_seen_poll;
  TrailPoint trail[TRAIL_CAPACITY];
  uint16_t trail_head;      // index where the next point is written
  uint16_t trail_len;
  bool stale;
  bool in_use;
  const MetadataRow *meta;
};
