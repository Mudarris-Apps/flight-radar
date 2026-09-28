#pragma once
#include <stdint.h>
enum class InputEvent : uint8_t { ROTATE_LEFT, ROTATE_RIGHT, PRESS, LONG_PRESS, TAP, DOUBLE_TAP, DRAG };
class ZoomController {
public:
  ZoomController();
  float viewRadiusM() const;
  float targetRadiusM() const;
  void  zoomIn(); void zoomOut();
  void  handleEvent(InputEvent e);      // ROTATE_RIGHT -> zoomIn, ROTATE_LEFT -> zoomOut, others ignored
  void  tick(uint32_t now_ms);          // eases current toward target over ZOOM_EASE_MS
  void  reset();
  bool  animating() const;
private:
  float current_, target_, from_; uint32_t ease_start_ms_, last_tick_ms_; bool easing_;
};
