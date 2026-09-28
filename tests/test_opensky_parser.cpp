#include "test.h"
#include "opensky_parser.h"
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
static std::string readFixture() {
  std::ifstream f("fixtures/states_sample.json"); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
TEST(parses_sample_fixture) {
  auto s = readFixture(); AircraftReport out[200]; ParsedStates info{};
  CHECK(parseOpenSkyStates(s.data(), s.size(), out, 200, info));
  CHECK(info.count == 58); CHECK(info.time == 1700000000u); CHECK(!info.states_null);
  CHECK_STREQ(out[0].icao24, "7c4e21"); CHECK_STREQ(out[0].callsign, "SYN100");
  CHECK(out[0].on_ground); CHECK(std::isnan(out[0].baro_alt_m)); CHECK(std::isnan(out[0].vertical_rate_mps));
  CHECK_NEAR(out[0].lat, -33.9402, 1e-4); CHECK_NEAR(out[0].lon, 151.1695, 1e-4);
  CHECK_STREQ(out[0].squawk, "");
  CHECK_STREQ(out[1].squawk, "3217"); CHECK_NEAR(out[1].baro_alt_m, 2438.4, 0.01);
}
TEST(null_position_row) {
  const char *j = R"({"time":10,"states":[["abc123",null,"X",null,10,null,null,null,false,null,null,null,null,null,null,false,0,0]]})";
  AircraftReport out[4]; ParsedStates info{};
  CHECK(parseOpenSkyStates(j, strlen(j), out, 4, info));
  CHECK(info.count == 1); CHECK(out[0].time_position == 0); CHECK(std::isnan(out[0].lat)); CHECK_STREQ(out[0].callsign, "");
}
TEST(states_null_is_valid_empty_poll) {
  const char *j = R"({"time":10,"states":null})";
  AircraftReport out[4]; ParsedStates info{};
  CHECK(parseOpenSkyStates(j, strlen(j), out, 4, info));
  CHECK(info.count == 0); CHECK(info.states_null); CHECK(info.time == 10u);
}
TEST(malformed_json_fails) {
  const char *j = "{\"time\":10,\"states\":[[";
  AircraftReport out[4]; ParsedStates info{};
  CHECK(!parseOpenSkyStates(j, strlen(j), out, 4, info));
}
TEST(caps_at_max_out) {
  auto s = readFixture(); AircraftReport out[10]; ParsedStates info{};
  CHECK(parseOpenSkyStates(s.data(), s.size(), out, 10, info)); CHECK(info.count == 10);
}
TEST(short_row_without_category) {
  const char *j = R"({"time":10,"states":[["abc123","QF1     ","AU",9,10,151.0,-33.9,100.0,false,200.0,90.0,1.0,null,120.0,"1200",false,0]]})";
  AircraftReport out[4]; ParsedStates info{};
  CHECK(parseOpenSkyStates(j, strlen(j), out, 4, info)); CHECK(out[0].category == 0); CHECK_STREQ(out[0].callsign, "QF1");
}
TEST_MAIN()
