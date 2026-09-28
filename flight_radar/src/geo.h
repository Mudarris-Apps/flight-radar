#pragma once
#include <stddef.h>
namespace geo {
struct Home { float lat, lon, cos_lat; };
struct LocalXY { float x, y; };            // metres east, north of home
struct BBox { float lamin, lamax, lomin, lomax; };
struct Projection { float cx, cy, r_px, view_radius_m, pan_x_m, pan_y_m; };

Home    makeHome(float lat, float lon);
LocalXY toLocal(const Home&, float lat, float lon);
float   distanceM(const Home&, float lat, float lon);
BBox    bbox(const Home&, float radius_m);
void    formatBBoxQuery(const BBox&, char *out, size_t n);   // "lamin=-34.8161&lamax=...&lomin=...&lomax=..."
void    project(const Home&, const Projection&, float lat, float lon, float &px, float &py);
bool    insideScope(const Projection&, float px, float py);   // within r_px of (cx,cy)
float   scalePxPerM(const Projection&);
}
