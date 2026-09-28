#include "zoom_controller.h"
#include "config.h"
#include <cmath>

namespace {
float smoothstep(float t) {
  if (t < 0.f) t = 0.f;
  if (t > 1.f) t = 1.f;
  return t * t * (3.f - 2.f * t);
}
}  // namespace

ZoomController::ZoomController()
    : current_(ZOOM_MAX_RADIUS_M),
      target_(ZOOM_MAX_RADIUS_M),
      from_(ZOOM_MAX_RADIUS_M),
      ease_start_ms_(0),
      last_tick_ms_(0),
      easing_(false) {}

float ZoomController::viewRadiusM() const { return current_; }
float ZoomController::targetRadiusM() const { return target_; }
bool ZoomController::animating() const { return easing_; }

void ZoomController::zoomIn() {
  float new_target = target_ / ZOOM_STEP;
  if (new_target < ZOOM_MIN_RADIUS_M) new_target = ZOOM_MIN_RADIUS_M;
  if (new_target == target_) return;  // already at clamp: target unchanged, no ease
  target_ = new_target;
  from_ = current_;
  ease_start_ms_ = last_tick_ms_;
  easing_ = true;
}

void ZoomController::zoomOut() {
  float new_target = target_ * ZOOM_STEP;
  if (new_target > ZOOM_MAX_RADIUS_M) new_target = ZOOM_MAX_RADIUS_M;
  if (new_target == target_) return;  // already at clamp: target unchanged, no ease
  target_ = new_target;
  from_ = current_;
  ease_start_ms_ = last_tick_ms_;
  easing_ = true;
}

void ZoomController::handleEvent(InputEvent e) {
  switch (e) {
    case InputEvent::ROTATE_RIGHT:
      zoomIn();
      break;
    case InputEvent::ROTATE_LEFT:
      zoomOut();
      break;
    default:
      break;
  }
}

void ZoomController::tick(uint32_t now_ms) {
  last_tick_ms_ = now_ms;
  if (!easing_) return;
  float raw_t = static_cast<float>(now_ms - ease_start_ms_) / static_cast<float>(ZOOM_EASE_MS);
  if (raw_t >= 1.f) {
    current_ = target_;
    easing_ = false;
    return;
  }
  float s = smoothstep(raw_t);
  current_ = std::exp(std::log(from_) + (std::log(target_) - std::log(from_)) * s);
}

void ZoomController::reset() {
  if (target_ == ZOOM_MAX_RADIUS_M) return;  // already at max: target unchanged, no ease
  target_ = ZOOM_MAX_RADIUS_M;
  from_ = current_;
  ease_start_ms_ = last_tick_ms_;
  easing_ = true;
}
