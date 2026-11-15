// selection.cpp: pure knob-press selection order (host-tested).
#include "selection.h"
#include <cmath>
#include <cstring>

int selectNextByDistance(const Aircraft *snap, size_t n, const geo::Home &home, const char *current) {
  if (!snap || n == 0) return -1;
  if (n > MAX_AIRCRAFT) n = MAX_AIRCRAFT;
  // Static scratch keeps ~1.3 KB off the LVGL task stack; only the LVGL task calls this.
  static int idx[MAX_AIRCRAFT];
  static float dist[MAX_AIRCRAFT];
  size_t m = 0;
  for (size_t i = 0; i < n; ++i) {
    const Aircraft &a = snap[i];
    if (!a.in_use || std::isnan(a.last.lat) || std::isnan(a.last.lon)) continue;
    float d = geo::distanceM(home, a.last.lat, a.last.lon);
    // Stable insertion sort: strictly-greater shifts keep equal distances in index order.
    size_t j = m++;
    while (j > 0 && dist[j - 1] > d) {
      idx[j] = idx[j - 1];
      dist[j] = dist[j - 1];
      --j;
    }
    idx[j] = (int)i;
    dist[j] = d;
  }
  if (m == 0) return -1;
  if (current && current[0]) {
    for (size_t k = 0; k < m; ++k) {
      if (strncmp(snap[idx[k]].last.icao24, current, sizeof(snap[0].last.icao24)) == 0)
        return k + 1 < m ? idx[k + 1] : -1;
    }
  }
  return idx[0];
}

int nearestAirborneWithin(const Aircraft *snap, const SpriteMotion *motion, size_t n, const geo::Home &home,
                          float radius_m, float *dist_m) {
  int best = -1;
  float best_d = radius_m;
  for (size_t i = 0; snap && i < n; ++i) {
    const Aircraft &a = snap[i];
    if (!a.in_use || a.last.on_ground) continue;
    float lat = a.last.lat, lon = a.last.lon;
    if (motion && motion[i].initialised) {
      lat = motion[i].cur_lat;
      lon = motion[i].cur_lon;
    }
    if (std::isnan(lat) || std::isnan(lon)) continue;
    float d = geo::distanceM(home, lat, lon);
    if (d <= best_d) {
      best_d = d;
      best = (int)i;
    }
  }
  if (dist_m) *dist_m = best >= 0 ? best_d : -1.f;
  return best;
}
