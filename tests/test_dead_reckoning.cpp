#include "test.h"
#include "dead_reckoning.h"
#include <cmath>
#include <cstring>
static AircraftReport rep(float lat, float lon, uint32_t tpos, float v, float trk, bool ground=false) {
  AircraftReport r{}; std::strcpy(r.icao24, "7c0001"); r.lat = lat; r.lon = lon; r.time_position = tpos; r.last_contact = tpos;
  r.velocity_mps = v; r.track_deg = trk; r.on_ground = ground; r.vertical_rate_mps = 0; r.baro_alt_m = 1000; r.geo_alt_m = NAN; return r;
}
TEST(sixty_seconds_east_at_250) {
  auto d = deadReckon(rep(-33.9f, 151.0f, 1000, 250, 90), 1060);
  CHECK(d.valid); CHECK(!d.frozen); CHECK_NEAR(d.lat, -33.9, 1e-5);
  float dlon = 15000.0f / (111320.0f * std::cos(-33.9f * (float)M_PI / 180)); CHECK_NEAR(d.lon, 151.0 + dlon, 2e-4);
}
TEST(caps_extrapolation_and_flags_frozen) {
  auto d = deadReckon(rep(-33.9f, 151.0f, 1000, 250, 0), 1000 + DR_MAX_EXTRAPOLATION_S + 300);
  CHECK(d.frozen); CHECK_NEAR(d.lat, -33.9 + 250.0 * DR_MAX_EXTRAPOLATION_S / 110574.0, 1e-5);
}
TEST(ground_and_nan_do_not_move) {
  auto g = deadReckon(rep(-33.9f, 151.0f, 1000, 10, 90, true), 1060); CHECK_NEAR(g.lon, 151.0, 1e-7);
  auto n = deadReckon(rep(-33.9f, 151.0f, 1000, NAN, 90), 1060); CHECK_NEAR(n.lon, 151.0, 1e-7);
  auto bad = deadReckon(rep(NAN, NAN, 1000, 10, 90), 1060); CHECK(!bad.valid);
}
TEST(ease_fraction_and_angle) {
  CHECK_NEAR(easeFraction(0, 1000), 0, 1e-6); CHECK_NEAR(easeFraction(500, 1000), 0.5, 1e-6); CHECK_NEAR(easeFraction(2000, 1000), 1, 1e-6);
  CHECK_NEAR(lerpAngleDeg(350, 10, 0.5), 0, 1e-4); CHECK_NEAR(lerpAngleDeg(10, 350, 0.5), 0, 1e-4); CHECK_NEAR(lerpAngleDeg(0, 180, 0.25), 45, 1e-4);
}
TEST(sprite_motion_eases_to_new_report) {
  SpriteMotion m{}; auto r1 = rep(-33.9f, 151.0f, 1000, 0, 0);
  spriteMotionUpdate(m, r1, 1000, 0); CHECK(m.initialised); CHECK_NEAR(m.cur_lat, -33.9, 2e-6);
  auto r2 = rep(-33.8f, 151.0f, 1025, 0, 0);
  spriteMotionUpdate(m, r2, 1025, 10000); CHECK(m.easing); CHECK_NEAR(m.cur_lat, -33.9, 2e-6);
  spriteMotionUpdate(m, r2, 1025, 10500); CHECK_NEAR(m.cur_lat, -33.85, 1e-4);
  spriteMotionUpdate(m, r2, 1026, 10000 + EASE_MS); CHECK(!m.easing); CHECK_NEAR(m.cur_lat, -33.8, 1e-6);
}
TEST_MAIN()
