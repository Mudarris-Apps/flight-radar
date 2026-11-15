#include "test.h"
#include "units.h"
#include "metadata_table.h"
TEST(m_to_ft) { CHECK_NEAR(mToFt(1000.f), 3280.84, 0.1); }
TEST(mps_to_kt) { CHECK_NEAR(mpsToKt(100.f), 194.38, 0.1); }
TEST(mps_to_fpm) { CHECK_NEAR(mpsToFpm(-2.6f), -511.8, 0.1); }
TEST(fmt_nan_is_unknown) {
  char b[16];
  fmtOrUnknown(b, sizeof(b), NAN, "%.0f ft");
  CHECK_STREQ(b, "Unknown");
}
TEST(fmt_value) {
  char b[16];
  fmtOrUnknown(b, sizeof(b), 2438.4f, "%.0f ft");
  CHECK_STREQ(b, "2438 ft");
}
TEST(metadata_absent_key) {
  MetadataRow m;
  CHECK(!metadataLookup("000000", m));
}
TEST(metadata_unparsable) {
  MetadataRow m;
  CHECK(!metadataLookup("zz", m));
  CHECK(!metadataLookup("", m));
  CHECK(!metadataLookup(nullptr, m));
}
TEST(metadata_first_last_present) {
  if (METADATA_COUNT == 0) return;
  char first[7], last[7];
  std::snprintf(first, sizeof(first), "%06x", (unsigned)metadataIcaoAt(0));
  std::snprintf(last, sizeof(last), "%06x", (unsigned)metadataIcaoAt(METADATA_COUNT - 1));
  MetadataRow r1, r2;
  CHECK(metadataLookup(first, r1));
  CHECK(metadataLookup(last, r2));
  CHECK(r1.reg[0] != '\0');
  CHECK(r2.reg[0] != '\0');
}
TEST(airline_name_needs_designator_callsign) {
  CHECK(airlineNameForCallsign(nullptr) == nullptr);
  CHECK(airlineNameForCallsign("") == nullptr);
  CHECK(airlineNameForCallsign("VHABC") == nullptr);
}
TEST_MAIN()
