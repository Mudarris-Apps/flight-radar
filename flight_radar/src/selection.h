#pragma once
#include <stddef.h>
#include "aircraft.h"
#include "dead_reckoning.h"
#include "geo.h"
// Returns index of the next aircraft by distance from home after `current` (icao24 or empty), or -1 when wrapping past the last.
// Aircraft with !in_use or a NaN position are skipped; ties keep the lower index first.
// A `current` that is empty, null or not selectable yields the nearest.
int selectNextByDistance(const Aircraft *snap, size_t n, const geo::Home &home, const char *current);
// Index of the nearest airborne aircraft within radius_m of home, or -1. Uses the
// on-screen (dead-reckoned) position in motion[i] when it is initialised, else the
// last report. dist_m (optional) gets its distance. For overhead mode.
int nearestAirborneWithin(const Aircraft *snap, const SpriteMotion *motion, size_t n, const geo::Home &home,
                          float radius_m, float *dist_m = nullptr);
