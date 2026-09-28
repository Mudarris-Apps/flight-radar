#include "test.h"
#include "epoch.h"
TEST(epoch_zero_before_first_poll) { CHECK(epochFrom(0, 0, 5000) == 0); }
TEST(epoch_advances_whole_seconds) { CHECK(epochFrom(1790568225u, 10000, 35000) == 1790568250u); }
TEST(epoch_truncates_partial_seconds) { CHECK(epochFrom(1000, 0, 1999) == 1001); }
TEST(epoch_survives_millis_wrap) {
  CHECK(epochFrom(100, 4294967000u, 1000) == 100 + (uint32_t)(1000 - 4294967000u) / 1000);
  CHECK(epochFrom(100, 4294967000u, 1000) == 101);
}
TEST_MAIN()
