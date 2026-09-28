// epoch.h: current OpenSky epoch from the last poll's epoch and millis() stamp (pure, host-tested).
#pragma once
#include <stdint.h>
inline uint32_t epochFrom(uint32_t last_poll_epoch, uint32_t last_poll_millis, uint32_t now_millis) {
  return last_poll_epoch == 0 ? 0 : last_poll_epoch + (now_millis - last_poll_millis) / 1000;
}
