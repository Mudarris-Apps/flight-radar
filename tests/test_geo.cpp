#include "test.h"
#include "geo.h"
#include <cstdio>
static const geo::Home H = geo::makeHome(-33.9461f, 151.1772f);

TEST(local_xy_of_home_is_origin) {
  auto p = geo::toLocal(H, -33.9461f, 151.1772f);
  CHECK_NEAR(p.x, 0, 0.01); CHECK_NEAR(p.y, 0, 0.01);
}
TEST(one_degree_north_is_110574_m) {
  auto p = geo::toLocal(H, -32.917f, 151.1772f);
  CHECK_NEAR(p.y, 110574, 1); CHECK_NEAR(p.x, 0, 0.01);
}
TEST(distance_to_yssy_is_about_14_km) {
  float d = geo::distanceM(H, -33.9461f, 151.1772f);
  CHECK(d > 13000 && d < 14500);   // 13.6 km by great circle
}
TEST(bbox_100km) {
  auto b = geo::bbox(H, 100000);
  CHECK_NEAR(b.lamin, -33.9461 - 0.9044, 0.002);
  CHECK_NEAR(b.lamax, -33.9461 + 0.9044, 0.002);
  CHECK_NEAR(b.lomax - b.lomin, 2 * 100000 / (111320 * std::cos(-33.9461 * M_PI / 180)), 0.002);
  char q[128]; geo::formatBBoxQuery(b, q, sizeof q);
  CHECK(std::strncmp(q, "lamin=-34.8214&lamax=-33.0126&lomin=", 36) == 0);
}
TEST(projection_centre_and_scale) {
  geo::Projection P{233, 233, 229, 100000, 0, 0};
  float px, py;
  geo::project(H, P, -33.9461f, 151.1772f, px, py);
  CHECK_NEAR(px, 233, 0.01); CHECK_NEAR(py, 233, 0.01);
  geo::project(H, P, -33.9461f + 100000.0f / 110574.0f, 151.1772f, px, py);   // 100 km north
  CHECK_NEAR(px, 233, 0.01); CHECK_NEAR(py, 233 - 229, 0.5);                // up on screen
  CHECK(geo::insideScope(P, 233, 233));
  CHECK(!geo::insideScope(P, 233, 3));
  CHECK_NEAR(geo::scalePxPerM(P), 229.0 / 100000.0, 1e-9);
}
TEST(pan_shifts_projection) {
  geo::Projection P{233, 233, 229, 100000, 10000, 0};    // pan 10 km east
  float px, py;
  geo::project(H, P, -33.9461f, 151.1772f, px, py);
  CHECK_NEAR(px, 233 + 229 * 0.1, 0.01);
}
TEST_MAIN()
