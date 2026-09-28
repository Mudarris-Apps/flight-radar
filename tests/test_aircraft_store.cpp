#include "test.h"
#include "aircraft_store.h"
#include <cstring>
#include <cmath>
static geo::Home H = geo::makeHome(-33.9461f, 151.1772f);
static AircraftReport rep(const char *icao, float lat, float lon, uint32_t tpos) {
  AircraftReport r{}; std::strncpy(r.icao24, icao, 6); r.lat = lat; r.lon = lon; r.time_position = tpos; r.last_contact = tpos;
  r.velocity_mps = 100; r.track_deg = 90; r.baro_alt_m = 1000; r.geo_alt_m = NAN; r.vertical_rate_mps = 0; return r;
}
TEST(upsert_keeps_identity_and_counts) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", -33.9f, 151.0f, 100)};
  s.applyPoll(a, 1, 100); CHECK(s.count() == 1); CHECK(s.generation() == 1);
  a[0].time_position = 125; s.applyPoll(a, 1, 125); CHECK(s.count() == 1); CHECK(s.generation() == 2);
  const Aircraft *x = s.find("7c0001"); CHECK(x != nullptr); CHECK(x->first_seen == 100); CHECK(x->last_seen_poll == 125); CHECK(x->trail_len == 2);
}
TEST(missing_goes_stale_then_removed) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", -33.9f, 151.0f, 100)};
  s.applyPoll(a, 1, 100); s.applyPoll(nullptr, 0, 125);
  CHECK(s.find("7c0001")->stale); CHECK(s.count() == 1);
  s.applyPoll(nullptr, 0, 100 + STALE_REMOVE_S); CHECK(s.find("7c0001") == nullptr); CHECK(s.count() == 0);
}
TEST(trail_appends_only_on_newer_time_position) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", -33.9f, 151.0f, 100)};
  s.applyPoll(a, 1, 100); s.applyPoll(a, 1, 125);            // same time_position
  CHECK(s.find("7c0001")->trail_len == 1);
  a[0].time_position = 0; a[0].lat = NAN; a[0].lon = NAN; s.applyPoll(a, 1, 150);   // null fix
  const Aircraft *x = s.find("7c0001"); CHECK(x->trail_len == 1); CHECK_NEAR(x->last.lat, -33.9, 1e-5); CHECK(x->last_seen_poll == 150); CHECK(!x->stale);
}
TEST(new_aircraft_without_position_is_dropped) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", NAN, NAN, 0)};
  s.applyPoll(a, 1, 100); CHECK(s.count() == 0);
}
TEST(out_of_radius_dropped) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", -36.0f, 151.0f, 100)};   // ~230 km south
  s.applyPoll(a, 1, 100); CHECK(s.count() == 0);
}
TEST(ring_buffer_wraps_and_window_is_oldest_first) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", -33.9f, 151.0f, 0)};
  for (uint32_t i = 1; i <= TRAIL_CAPACITY + 10; i++) { a[0].time_position = i * 10; a[0].lat = -33.9f + i * 1e-4f; s.applyPoll(a, 1, i * 10); }
  const Aircraft *x = s.find("7c0001"); CHECK(x->trail_len == TRAIL_CAPACITY);
  TrailPoint out[TRAIL_CAPACITY]; uint32_t now = (TRAIL_CAPACITY + 10) * 10;
  size_t n = AircraftStore::trailWindow(*x, now, 600, out, TRAIL_CAPACITY);
  CHECK(n == 61);                                       // t in [now-600, now] inclusive at 10 s spacing
  for (size_t i = 1; i < n; i++) CHECK(out[i].t > out[i - 1].t);
  CHECK(out[n - 1].t == now);
  size_t all = AircraftStore::trailWindow(*x, now, 1000000, out, TRAIL_CAPACITY); CHECK(all == TRAIL_CAPACITY);
  CHECK(out[0].t == (uint32_t)(10 + 1) * 10 && out[0].t == 110);   // 10 oldest were overwritten
}
TEST(snapshot_copies_in_use_only) {
  AircraftStore s(H, 100000); AircraftReport a[2] = {rep("7c0001", -33.9f, 151.0f, 100), rep("7c0002", -33.8f, 151.1f, 100)};
  s.applyPoll(a, 2, 100); Aircraft out[MAX_AIRCRAFT]; CHECK(s.snapshot(out, MAX_AIRCRAFT) == 2); CHECK(out[0].in_use && out[1].in_use);
}
TEST(store_full_drops_extra) {
  AircraftStore s(H, 100000); AircraftReport a[MAX_AIRCRAFT + 5];
  for (int i = 0; i < MAX_AIRCRAFT + 5; i++) { char id[7]; std::snprintf(id, 7, "%06x", 0x7c0000 + i); a[i] = rep(id, -33.9f, 151.0f, 100); }
  s.applyPoll(a, MAX_AIRCRAFT + 5, 100); CHECK(s.count() == MAX_AIRCRAFT);
}
TEST_MAIN()
