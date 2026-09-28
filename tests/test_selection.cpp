#include "test.h"
#include "selection.h"
#include <cstring>
static const geo::Home H = geo::makeHome(-33.9461f, 151.1772f);

// Aircraft `km` kilometres due north of home.
static void place(Aircraft &a, const char *icao, float km) {
  std::memset(&a, 0, sizeof a);
  std::strncpy(a.last.icao24, icao, sizeof a.last.icao24 - 1);
  a.last.lat = -33.9461f + km * 1000.0f / 110574.0f;
  a.last.lon = 151.1772f;
  a.in_use = true;
}

// Deliberately out of distance order: index 0 = 50 km, 1 = 5 km, 2 = 20 km.
static void fill(Aircraft *s) {
  place(s[0], "aaa050", 50);
  place(s[1], "bbb005", 5);
  place(s[2], "ccc020", 20);
}

TEST(empty_current_selects_nearest) {
  Aircraft s[3]; fill(s);
  CHECK(selectNextByDistance(s, 3, H, "") == 1);
}
TEST(null_current_selects_nearest) {
  Aircraft s[3]; fill(s);
  CHECK(selectNextByDistance(s, 3, H, nullptr) == 1);
}
TEST(from_nearest_goes_to_next) {
  Aircraft s[3]; fill(s);
  CHECK(selectNextByDistance(s, 3, H, "bbb005") == 2);
  CHECK(selectNextByDistance(s, 3, H, "ccc020") == 0);
}
TEST(from_farthest_wraps_to_none) {
  Aircraft s[3]; fill(s);
  CHECK(selectNextByDistance(s, 3, H, "aaa050") == -1);
}
TEST(unknown_current_selects_nearest) {
  Aircraft s[3]; fill(s);
  CHECK(selectNextByDistance(s, 3, H, "zzz999") == 1);
}
TEST(n_zero_is_none) {
  Aircraft s[3]; fill(s);
  CHECK(selectNextByDistance(s, 0, H, "") == -1);
  CHECK(selectNextByDistance(nullptr, 0, H, "") == -1);
}
TEST(not_in_use_is_skipped) {
  Aircraft s[3]; fill(s);
  s[1].in_use = false;   // the 5 km one
  CHECK(selectNextByDistance(s, 3, H, "") == 2);
  CHECK(selectNextByDistance(s, 3, H, "ccc020") == 0);
  CHECK(selectNextByDistance(s, 3, H, "bbb005") == 2);   // current not selectable -> nearest
}
TEST(nan_position_is_skipped) {
  Aircraft s[3]; fill(s);
  s[1].last.lat = NAN;
  CHECK(selectNextByDistance(s, 3, H, "") == 2);
  s[2].last.lon = NAN;
  CHECK(selectNextByDistance(s, 3, H, "") == 0);
  CHECK(selectNextByDistance(s, 3, H, "aaa050") == -1);
}
TEST(all_excluded_is_none) {
  Aircraft s[3]; fill(s);
  for (auto &a : s) a.in_use = false;
  CHECK(selectNextByDistance(s, 3, H, "") == -1);
}
TEST(ties_keep_lower_index_first) {
  Aircraft s[3];
  place(s[0], "t00010", 10);
  place(s[1], "t10010", 10);
  place(s[2], "t20003", 3);
  CHECK(selectNextByDistance(s, 3, H, "") == 2);
  CHECK(selectNextByDistance(s, 3, H, "t20003") == 0);
  CHECK(selectNextByDistance(s, 3, H, "t00010") == 1);
  CHECK(selectNextByDistance(s, 3, H, "t10010") == -1);
}
TEST(full_cycle_over_max_aircraft) {
  static Aircraft s[MAX_AIRCRAFT];
  char icao[7];
  for (int i = 0; i < MAX_AIRCRAFT; ++i) {
    std::snprintf(icao, sizeof icao, "%06x", i);
    place(s[i], icao, (float)((i * 37) % MAX_AIRCRAFT) + 1.0f);   // a permutation of distances
  }
  int visited = 0, idx = selectNextByDistance(s, MAX_AIRCRAFT, H, "");
  float prev = -1.0f;
  while (idx >= 0 && visited <= MAX_AIRCRAFT) {
    float d = geo::distanceM(H, s[idx].last.lat, s[idx].last.lon);
    CHECK(d > prev);
    prev = d;
    ++visited;
    idx = selectNextByDistance(s, MAX_AIRCRAFT, H, s[idx].last.icao24);
  }
  CHECK(visited == MAX_AIRCRAFT);
}
TEST_MAIN()
