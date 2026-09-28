#include "airports.h"
#include <algorithm>

size_t airportsInBox(float lamin, float lamax, float lomin, float lomax, const Airport **out, size_t max) {
  const Airport *begin = AIRPORTS;
  const Airport *end = AIRPORTS + AIRPORTS_COUNT;
  const Airport *first = std::lower_bound(begin, end, lamin, [](const Airport &a, float lat) { return a.lat < lat; });
  size_t n = 0;
  for (const Airport *p = first; p != end && p->lat <= lamax && n < max; ++p) {
    if (p->lon >= lomin && p->lon <= lomax) {
      out[n++] = p;
    }
  }
  return n;
}
