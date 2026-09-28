#pragma once
#include <stddef.h>
#include "aircraft.h"
#include "geo.h"
// Returns index of the next aircraft by distance from home after `current` (icao24 or empty), or -1 when wrapping past the last.
// Aircraft with !in_use or a NaN position are skipped; ties keep the lower index first.
// A `current` that is empty, null or not selectable yields the nearest.
int selectNextByDistance(const Aircraft *snap, size_t n, const geo::Home &home, const char *current);
