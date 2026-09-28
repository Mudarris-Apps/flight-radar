#pragma once
#include <stdint.h>
#include "aircraft_store.h"
enum class NetState : uint8_t { WIFI_CONNECTING, WIFI_DOWN, AUTH_FAILED, RATE_LIMITED, HTTP_ERROR, OK };
struct NetStatus {
  NetState state;
  uint32_t last_poll_epoch;    // OpenSky "time" field of last successful poll
  uint32_t last_poll_millis;   // millis() when that poll was applied
  uint16_t aircraft_count;
  int      rate_remaining;     // -1 unknown
  uint32_t next_poll_in_s;
  uint16_t http_code;
};
void      netTaskStart(AircraftStore *store);
NetStatus netStatusGet();
uint32_t  netNowEpoch();       // last_poll_epoch + (millis()-last_poll_millis)/1000, 0 before first poll
