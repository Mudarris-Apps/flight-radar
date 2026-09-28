#include "test.h"
#include "config.h"
TEST(config_defaults_present) { CHECK(POLL_INTERVAL_S == 25); CHECK(TRAIL_CAPACITY == 96); }
TEST_MAIN()
