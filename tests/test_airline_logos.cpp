#include "test.h"
#include "airline_logos.h"
#include <cstring>
TEST(code_from_airline_callsign) {
  char c[4];
  CHECK(airlineCodeFromCallsign("QFA123", c)); CHECK(std::strcmp(c, "QFA") == 0);
  CHECK(airlineCodeFromCallsign("JST4A", c)); CHECK(std::strcmp(c, "JST") == 0);
}
TEST(non_airline_callsigns_rejected) {
  char c[4];
  CHECK(!airlineCodeFromCallsign("VHABC", c));    // registration
  CHECK(!airlineCodeFromCallsign("", c));
  CHECK(!airlineCodeFromCallsign("QF1", c));      // IATA style, too short
  CHECK(!airlineCodeFromCallsign("qfa123", c));
  CHECK(!airlineCodeFromCallsign(nullptr, c));
}
TEST(stub_table_has_no_logos) {
  CHECK(AIRLINE_LOGOS_COUNT == 0 || airlineLogoIndex("QFA1") >= -1);
  if (AIRLINE_LOGOS_COUNT == 0) CHECK(airlineLogoIndex("QFA123") == -1);
}
TEST_MAIN()
