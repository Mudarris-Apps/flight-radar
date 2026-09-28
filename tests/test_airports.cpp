#include "test.h"
#include "airports.h"
#include <cstring>
TEST(sorted_by_lat) { for (size_t i = 1; i < AIRPORTS_COUNT; i++) CHECK(AIRPORTS[i].lat >= AIRPORTS[i - 1].lat); CHECK(AIRPORTS_COUNT > 5000); }
TEST(sydney_box_contains_yssy_and_ysbk) {
  const Airport *out[64]; size_t n = airportsInBox(-34.82f, -33.01f, 149.95f, 152.12f, out, 64);
  bool yssy = false, ysbk = false; for (size_t i = 0; i < n; i++) { if (!std::strcmp(out[i]->ident, "YSSY")) yssy = out[i]->large == 1; if (!std::strcmp(out[i]->ident, "YSBK")) ysbk = true; }
  CHECK(yssy); CHECK(ysbk); CHECK(n >= 5 && n <= 20);
}
TEST(empty_box) { const Airport *out[4]; CHECK(airportsInBox(-89.9f, -89.8f, 0, 1, out, 4) == 0); }
TEST_MAIN()
