#include "geo.h"
#include <cmath>
#include <cstdio>
namespace geo {
static constexpr float M_PER_DEG_LAT = 110574.0f;
static constexpr float M_PER_DEG_LON_EQ = 111320.0f;
Home makeHome(float lat, float lon) { return Home{lat, lon, std::cos(lat * (float)M_PI / 180.0f)}; }
LocalXY toLocal(const Home &h, float lat, float lon) {
  return LocalXY{(lon - h.lon) * h.cos_lat * M_PER_DEG_LON_EQ, (lat - h.lat) * M_PER_DEG_LAT};
}
float distanceM(const Home &h, float lat, float lon) { auto p = toLocal(h, lat, lon); return std::sqrt(p.x * p.x + p.y * p.y); }
BBox bbox(const Home &h, float radius_m) {
  float dlat = radius_m / M_PER_DEG_LAT;
  float dlon = radius_m / (M_PER_DEG_LON_EQ * h.cos_lat);
  return BBox{h.lat - dlat, h.lat + dlat, h.lon - dlon, h.lon + dlon};
}
void formatBBoxQuery(const BBox &b, char *out, size_t n) {
  std::snprintf(out, n, "lamin=%.4f&lamax=%.4f&lomin=%.4f&lomax=%.4f", b.lamin, b.lamax, b.lomin, b.lomax);
}
float scalePxPerM(const Projection &p) { return p.r_px / p.view_radius_m; }
void project(const Home &h, const Projection &p, float lat, float lon, float &px, float &py) {
  auto l = toLocal(h, lat, lon); float s = scalePxPerM(p);
  px = p.cx + (l.x + p.pan_x_m) * s;
  py = p.cy - (l.y + p.pan_y_m) * s;
}
bool insideScope(const Projection &p, float px, float py) {
  float dx = px - p.cx, dy = py - p.cy; return dx * dx + dy * dy <= p.r_px * p.r_px;
}
}
