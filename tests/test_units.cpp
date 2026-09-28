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
TEST(metadata_stub_absent) { CHECK(metadataLookup("7c4e21") == nullptr); }
TEST(metadata_unparsable) {
  CHECK(metadataLookup("zz") == nullptr);
  CHECK(metadataLookup("") == nullptr);
  CHECK(metadataLookup(nullptr) == nullptr);
}
TEST_MAIN()
