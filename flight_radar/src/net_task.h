#pragma once
#include <stddef.h>
#include <stdint.h>
#include "aircraft_store.h"
#include "geo.h"
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

// Wi-Fi setup (thread-safe; called from the LVGL task, carried out by the net task).
const int WIFI_SCAN_MAX = 16;
struct WifiScanEntry {
  char   ssid[33];
  int8_t rssi;
  bool   open;    // no password needed
  bool   known;   // saved on the device or the secrets.h home network
};
enum class WifiJoin : uint8_t { IDLE, BUSY, OK, FAILED };
void     netWifiRequestScan();
// Copies the latest scan (strongest first, one entry per SSID). `gen` changes with each new scan.
int      netWifiScanResults(WifiScanEntry *out, int max, uint32_t *gen, bool *scanning);
// Joins `ssid` (empty `pass` = open network). Saved to NVS only once the join succeeds.
void     netWifiRequestJoin(const char *ssid, const char *pass);
WifiJoin netWifiJoinStatus();
bool     netWifiNeedsSetup();  // true while no known network is in range or every known one failed
void     netWifiConnectedSsid(char *out, size_t n);   // "" when not connected

// Radar centre for the current network; `gen` changes whenever it moves.
geo::Home netHome(uint32_t *gen);
void      netPlace(char *out, size_t n);   // "home", a Wi-Fi fix with its accuracy, or the IP-located city
