#include "test.h"
#include "zoom_controller.h"
#include "config.h"
TEST(starts_at_max) { ZoomController z; CHECK_NEAR(z.viewRadiusM(), ZOOM_MAX_RADIUS_M, 1); CHECK_NEAR(z.targetRadiusM(), ZOOM_MAX_RADIUS_M, 1); }
TEST(zoom_in_divides_by_step_and_clamps) {
  ZoomController z; z.zoomIn(); CHECK_NEAR(z.targetRadiusM(), ZOOM_MAX_RADIUS_M / ZOOM_STEP, 1);
  for (int i = 0; i < 100; i++) z.zoomIn(); CHECK_NEAR(z.targetRadiusM(), ZOOM_MIN_RADIUS_M, 1);
  for (int i = 0; i < 100; i++) z.zoomOut(); CHECK_NEAR(z.targetRadiusM(), ZOOM_MAX_RADIUS_M, 1);
}
TEST(events_map) {
  ZoomController z; z.handleEvent(InputEvent::ROTATE_RIGHT); CHECK(z.targetRadiusM() < ZOOM_MAX_RADIUS_M);
  float t = z.targetRadiusM(); z.handleEvent(InputEvent::PRESS); CHECK_NEAR(z.targetRadiusM(), t, 0.01);
  z.handleEvent(InputEvent::ROTATE_LEFT); CHECK_NEAR(z.targetRadiusM(), ZOOM_MAX_RADIUS_M, 1);
}
TEST(easing_converges_in_ease_ms) {
  ZoomController z; z.tick(1000); z.zoomIn(); z.tick(1000);
  CHECK(z.animating()); CHECK_NEAR(z.viewRadiusM(), ZOOM_MAX_RADIUS_M, 1);
  z.tick(1000 + ZOOM_EASE_MS / 2); CHECK(z.viewRadiusM() < ZOOM_MAX_RADIUS_M && z.viewRadiusM() > z.targetRadiusM());
  z.tick(1000 + ZOOM_EASE_MS); CHECK(!z.animating()); CHECK_NEAR(z.viewRadiusM(), z.targetRadiusM(), 0.01);
}
TEST(full_range_is_about_23_detents) {
  ZoomController z; int n = 0; while (z.targetRadiusM() > ZOOM_MIN_RADIUS_M + 1 && n < 100) { z.zoomIn(); n++; } CHECK(n >= 22 && n <= 24);
}
TEST(reset_returns_to_max) { ZoomController z; z.zoomIn(); z.zoomIn(); z.reset(); CHECK_NEAR(z.targetRadiusM(), ZOOM_MAX_RADIUS_M, 1); }
TEST_MAIN()
