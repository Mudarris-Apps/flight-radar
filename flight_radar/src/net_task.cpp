#include "net_task.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstring>
#include "../secrets.h"
#include "config.h"
#include "epoch.h"
#include "geo.h"
#include "opensky_client.h"
#include "opensky_parser.h"
#include "wifi_store.h"

namespace {

const uint32_t WIFI_ATTEMPT_MS = 10000;
const uint32_t WIFI_JOIN_MS = 15000;       // a network typed in on the device gets a little longer
const uint32_t WIFI_RESCAN_S = 30;         // while no known network is in range
const size_t REPORTS_CAPACITY = MAX_AIRCRAFT + 64;

NetStatus g_status = {NetState::WIFI_CONNECTING, 0, 0, 0, -1, 0, 0};
SemaphoreHandle_t g_mutex = nullptr;
AircraftStore *g_store = nullptr;
bool g_have_poll = false;

// Wi-Fi setup state, all guarded by g_mutex.
WifiScanEntry g_scan[WIFI_SCAN_MAX];
int g_scan_n = 0;
uint32_t g_scan_gen = 0;
bool g_scanning = false;
bool g_scan_req = false;
bool g_join_req = false;
wifistore::Cred g_join = {};
WifiJoin g_join_status = WifiJoin::IDLE;
bool g_needs_setup = false;
char g_connected_ssid[33] = "";
// Off at boot: the user picks the network on the Wi-Fi screen. After the first
// successful join, a dropped connection reconnects to known networks by itself.
bool g_auto_reconnect = false;

// Radar centre: secrets.h HOME_LAT/HOME_LON on the home network, else located from
// the network's public IP on every join (a phone hotspot moves with you).
geo::Home g_home = geo::makeHome(HOME_LAT, HOME_LON);
uint32_t g_home_gen = 0;
char g_place[40] = "home";
char g_query[128] = "";

bool commandPending() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  bool p = g_scan_req || g_join_req;
  xSemaphoreGive(g_mutex);
  return p;
}

void setNeedsSetup(bool v) {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  bool prev = g_needs_setup;
  g_needs_setup = v;
  xSemaphoreGive(g_mutex);
  if (prev != v) Serial.printf("[net] wifi setup %s\n", v ? "needed" : "not needed");
}

void setConnectedSsid(const char *ssid) {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  strncpy(g_connected_ssid, ssid, sizeof(g_connected_ssid) - 1);
  g_connected_ssid[sizeof(g_connected_ssid) - 1] = '\0';
  xSemaphoreGive(g_mutex);
}

const char *stateName(NetState s) {
  switch (s) {
    case NetState::WIFI_CONNECTING: return "WIFI_CONNECTING";
    case NetState::WIFI_DOWN:       return "WIFI_DOWN";
    case NetState::AUTH_FAILED:     return "AUTH_FAILED";
    case NetState::RATE_LIMITED:    return "RATE_LIMITED";
    case NetState::HTTP_ERROR:      return "HTTP_ERROR";
    case NetState::OK:              return "OK";
  }
  return "?";
}

void setState(NetState s) {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  NetState prev = g_status.state;
  g_status.state = s;
  xSemaphoreGive(g_mutex);
  if (prev != s) Serial.printf("[net] state %s -> %s\n", stateName(prev), stateName(s));
}

void setNextPoll(uint32_t s) {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_status.next_poll_in_s = s;
  xSemaphoreGive(g_mutex);
}

void setHttpCode(int code) {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_status.http_code = code > 0 ? (uint16_t)code : 0;
  xSemaphoreGive(g_mutex);
}

// Waits `s` seconds, returning early when the UI asks for a scan or a join.
void sleepSeconds(uint32_t s) {
  for (uint32_t left = s; left > 0; --left) {
    setNextPoll(left);
    for (int q = 0; q < 4; ++q) {
      if (commandPending()) {
        setNextPoll(0);
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(250));
    }
  }
  setNextPoll(0);
}

unsigned freeHeap() { return (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
unsigned freePsram() { return (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

// Synchronous scan; publishes one entry per SSID, strongest first.
void doScan() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_scanning = true;
  g_scan_req = false;
  xSemaphoreGive(g_mutex);

  int16_t n = WiFi.scanNetworks(false, false);
  wifistore::Cred known[wifistore::MAX_SAVED + 1];
  int nk = wifistore::known(known, wifistore::MAX_SAVED + 1);
  WifiScanEntry out[WIFI_SCAN_MAX];
  int m = 0;
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0 || ssid.length() > 32) continue;   // hidden or malformed
    int8_t rssi = (int8_t)WiFi.RSSI(i);
    int dup = -1;
    for (int j = 0; j < m; ++j)
      if (strcmp(out[j].ssid, ssid.c_str()) == 0) dup = j;
    if (dup >= 0) {   // same network from several access points: keep the strongest
      if (rssi > out[dup].rssi) out[dup].rssi = rssi;
      continue;
    }
    if (m == WIFI_SCAN_MAX) {   // full: replace the weakest if this one is stronger
      int w = 0;
      for (int j = 1; j < m; ++j)
        if (out[j].rssi < out[w].rssi) w = j;
      if (rssi <= out[w].rssi) continue;
      dup = w;
    } else {
      dup = m++;
    }
    WifiScanEntry &e = out[dup];
    strncpy(e.ssid, ssid.c_str(), sizeof(e.ssid) - 1);
    e.ssid[sizeof(e.ssid) - 1] = '\0';
    e.rssi = rssi;
    e.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    e.known = false;
    for (int k = 0; k < nk; ++k)
      if (strcmp(known[k].ssid, e.ssid) == 0) e.known = true;
  }
  WiFi.scanDelete();
  for (int i = 1; i < m; ++i)   // insertion sort, strongest first
    for (int j = i; j > 0 && out[j].rssi > out[j - 1].rssi; --j) {
      WifiScanEntry t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
    }

  xSemaphoreTake(g_mutex, portMAX_DELAY);
  memcpy(g_scan, out, sizeof(WifiScanEntry) * m);
  g_scan_n = m;
  ++g_scan_gen;
  g_scanning = false;
  xSemaphoreGive(g_mutex);
  Serial.printf("[net] wifi: scan found %d networks (%d shown)\n", (int)n, m);
}

// One connection attempt; gives up early if `abortable` and the UI asks for something.
bool wifiTry(const char *ssid, const char *pass, uint32_t timeout_ms, bool abortable) {
  Serial.printf("[net] wifi: trying \"%s\" heap=%u psram=%u\n", ssid, freeHeap(), freePsram());
  WiFi.disconnect(false, false);
  vTaskDelay(pdMS_TO_TICKS(100));
  WiFi.begin(ssid, pass && pass[0] ? pass : nullptr);
  WiFi.setSleep(false);
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[net] wifi: connected to \"%s\" ip=%s rssi=%d\n", ssid,
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      setConnectedSsid(ssid);
      return true;
    }
    if (abortable && commandPending()) break;
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  Serial.printf("[net] wifi: \"%s\" failed, status=%d\n", ssid, (int)WiFi.status());
  WiFi.disconnect(false, false);
  return false;
}

bool isHomeNetwork(const char *ssid) { return strcmp(ssid, WIFI_SSID) == 0; }

// Approximate location of the current internet connection (city level).
bool ipLocate(float &lat, float &lon, char *place, size_t place_n) {
  HTTPClient http;
  http.setTimeout(6000);
  if (!http.begin("http://ip-api.com/json/?fields=status,lat,lon,city")) return false;
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString()) && doc["status"] == "success") {
      lat = doc["lat"].as<float>();
      lon = doc["lon"].as<float>();
      strncpy(place, doc["city"] | "unknown", place_n - 1);
      place[place_n - 1] = '\0';
      ok = lat >= -90.f && lat <= 90.f && lon >= -180.f && lon <= 180.f && !(lat == 0.f && lon == 0.f);
    }
  }
  http.end();
  Serial.printf("[net] locate: http=%d %s\n", code, ok ? place : "failed");
  return ok;
}

// Re-centres the radar for the network just joined. Keeps the old centre if the lookup fails.
void relocate(const char *ssid) {
  float lat, lon;
  char place[40];
  if (isHomeNetwork(ssid)) {
    lat = HOME_LAT;
    lon = HOME_LON;
    strncpy(place, "home", sizeof(place));
  } else if (!ipLocate(lat, lon, place, sizeof(place))) {
    return;
  }
  geo::Home h = geo::makeHome(lat, lon);
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  bool moved = geo::distanceM(g_home, lat, lon) > 1000.f;   // ignore tiny wobble between lookups
  strncpy(g_place, place, sizeof(g_place) - 1);
  g_place[sizeof(g_place) - 1] = '\0';
  if (moved) {
    g_home = h;
    ++g_home_gen;
  }
  xSemaphoreGive(g_mutex);
  if (!moved) return;
  g_store->setHome(h);
  geo::formatBBoxQuery(geo::bbox(h, OBS_RADIUS_M), g_query, sizeof(g_query));
  Serial.printf("[net] radar centre moved to %s (%.4f, %.4f)\n", place, lat, lon);
}

// No auto-connect yet: keep the scan list fresh for the Wi-Fi screen and wait for a choice.
void waitForUser() {
  setState(NetState::WIFI_DOWN);
  setConnectedSsid("");
  setNeedsSetup(true);
  doScan();
  sleepSeconds(WIFI_RESCAN_S);
}

// Scans, then tries every known network in range, strongest first. When none
// works, flags setup as needed and waits (waking early for UI commands).
void connectKnown() {
  setState(NetState::WIFI_CONNECTING);
  setConnectedSsid("");
  doScan();
  WifiScanEntry scan[WIFI_SCAN_MAX];
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  int n = g_scan_n;
  memcpy(scan, g_scan, sizeof(WifiScanEntry) * n);
  xSemaphoreGive(g_mutex);

  for (int i = 0; i < n; ++i) {
    if (!scan[i].known) continue;
    char pass[wifistore::PASS_MAX + 1];
    if (!wifistore::find(scan[i].ssid, pass, sizeof(pass))) continue;
    if (wifiTry(scan[i].ssid, pass, WIFI_ATTEMPT_MS, true)) {
      setNeedsSetup(false);
      relocate(scan[i].ssid);
      return;
    }
    if (commandPending()) return;   // the UI wants a join or scan: handle it first
  }
  setState(NetState::WIFI_DOWN);
  setNeedsSetup(true);
  sleepSeconds(WIFI_RESCAN_S);
}

// Carries out a pending UI join or scan request. Returns true if it did anything.
bool handleCommands() {
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  bool join = g_join_req, scan = g_scan_req;
  wifistore::Cred c = g_join;
  g_join_req = false;
  xSemaphoreGive(g_mutex);

  if (join) {
    setState(NetState::WIFI_CONNECTING);
    setConnectedSsid("");
    bool ok = wifiTry(c.ssid, c.pass, WIFI_JOIN_MS, false);
    if (ok) {
      wifistore::save(c.ssid, c.pass);
      setNeedsSetup(false);
      g_auto_reconnect = true;
      relocate(c.ssid);
    }
    memset(&c, 0, sizeof(c));
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    memset(&g_join, 0, sizeof(g_join));
    g_join_status = ok ? WifiJoin::OK : WifiJoin::FAILED;
    xSemaphoreGive(g_mutex);
    return true;
  }
  if (scan) {
    doScan();
    return true;
  }
  return false;
}

void netTask(void *) {
  AircraftReport *reports =
      (AircraftReport *)heap_caps_malloc(sizeof(AircraftReport) * REPORTS_CAPACITY, MALLOC_CAP_SPIRAM);
  if (!reports) {
    Serial.println("[net] FATAL: cannot allocate reports array in PSRAM");
    vTaskDelete(nullptr);
    return;
  }

  geo::formatBBoxQuery(geo::bbox(g_home, OBS_RADIUS_M), g_query, sizeof(g_query));
  Serial.printf("[net] task started, bbox %s\n", g_query);

  OpenSkyClient client(OPENSKY_CLIENT_ID, OPENSKY_CLIENT_SECRET);
  WiFi.mode(WIFI_STA);

  for (;;) {
    if (handleCommands()) continue;
    if (WiFi.status() != WL_CONNECTED) {
      if (g_auto_reconnect) connectKnown();
      else waitForUser();
      continue;
    }

    if (!client.ensureToken()) {
      setState(NetState::AUTH_FAILED);
      Serial.printf("[net] auth failed, retry in %us heap=%u psram=%u\n", (unsigned)POLL_ERROR_RETRY_S,
                    freeHeap(), freePsram());
      sleepSeconds(POLL_ERROR_RETRY_S);
      continue;
    }

    char *body = nullptr;
    size_t len = 0;
    int rate = -1;
    int code = client.fetchStates(g_query, &body, &len, &rate);
    uint32_t wait = POLL_ERROR_RETRY_S;
    setHttpCode(code);

    if (code == 200) {
      ParsedStates info;
      if (parseOpenSkyStates(body, len, reports, REPORTS_CAPACITY, info)) {
        g_store->applyPoll(reports, info.count, info.time);
        size_t n = g_store->count();
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        g_status.last_poll_epoch = info.time;
        g_status.last_poll_millis = millis();
        g_status.aircraft_count = (uint16_t)n;
        g_status.rate_remaining = rate;
        g_have_poll = true;
        xSemaphoreGive(g_mutex);
        setState(NetState::OK);
        wait = (rate >= 0 && rate < RATE_REMAINING_FLOOR) ? POLL_BACKOFF_S : POLL_INTERVAL_S;
      } else {
        Serial.printf("[net] states: malformed JSON (%u bytes)\n", (unsigned)len);
        setState(NetState::HTTP_ERROR);
      }
    } else if (code == 401) {
      client.invalidateToken();
      setState(NetState::AUTH_FAILED);
    } else if (code == 429) {
      setState(NetState::RATE_LIMITED);
      wait = POLL_BACKOFF_S;
    } else {
      setState(NetState::HTTP_ERROR);
    }
    if (code != 200 && rate >= 0) {
      xSemaphoreTake(g_mutex, portMAX_DELAY);
      g_status.rate_remaining = rate;
      xSemaphoreGive(g_mutex);
    }

    free(body);
    Serial.printf("[net] http=%d rate=%d aircraft=%u wait=%us heap=%u psram=%u\n", code, rate,
                  (unsigned)g_store->count(), (unsigned)wait, freeHeap(), freePsram());
    sleepSeconds(wait);
  }
}

}  // namespace

void netTaskStart(AircraftStore *store) {
  if (g_mutex) return;
  g_store = store;
  g_mutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(netTask, "net", 24576, nullptr, 1, nullptr, 0);
}

NetStatus netStatusGet() {
  if (!g_mutex) return g_status;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  NetStatus copy = g_status;
  xSemaphoreGive(g_mutex);
  return copy;
}

uint32_t netNowEpoch() {
  if (!g_mutex) return 0;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  bool have = g_have_poll;
  uint32_t epoch = g_status.last_poll_epoch, at = g_status.last_poll_millis;
  xSemaphoreGive(g_mutex);
  if (!have) return 0;
  return epochFrom(epoch, at, millis());
}

void netWifiRequestScan() {
  if (!g_mutex) return;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_scan_req = true;
  xSemaphoreGive(g_mutex);
}

int netWifiScanResults(WifiScanEntry *out, int max, uint32_t *gen, bool *scanning) {
  if (!g_mutex) return 0;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  int n = g_scan_n < max ? g_scan_n : max;
  memcpy(out, g_scan, sizeof(WifiScanEntry) * n);
  if (gen) *gen = g_scan_gen;
  if (scanning) *scanning = g_scanning || g_scan_req;
  xSemaphoreGive(g_mutex);
  return n;
}

void netWifiRequestJoin(const char *ssid, const char *pass) {
  if (!g_mutex) return;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  strncpy(g_join.ssid, ssid, sizeof(g_join.ssid) - 1);
  g_join.ssid[sizeof(g_join.ssid) - 1] = '\0';
  strncpy(g_join.pass, pass ? pass : "", sizeof(g_join.pass) - 1);
  g_join.pass[sizeof(g_join.pass) - 1] = '\0';
  g_join_req = true;
  g_join_status = WifiJoin::BUSY;
  xSemaphoreGive(g_mutex);
}

WifiJoin netWifiJoinStatus() {
  if (!g_mutex) return WifiJoin::IDLE;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  WifiJoin j = g_join_status;
  xSemaphoreGive(g_mutex);
  return j;
}

bool netWifiNeedsSetup() {
  if (!g_mutex) return false;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  bool v = g_needs_setup;
  xSemaphoreGive(g_mutex);
  return v;
}

void netWifiConnectedSsid(char *out, size_t n) {
  if (!n) return;
  if (!g_mutex) { out[0] = '\0'; return; }
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  strncpy(out, g_connected_ssid, n - 1);
  out[n - 1] = '\0';
  xSemaphoreGive(g_mutex);
}

geo::Home netHome(uint32_t *gen) {
  if (!g_mutex) { if (gen) *gen = 0; return g_home; }
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  geo::Home h = g_home;
  if (gen) *gen = g_home_gen;
  xSemaphoreGive(g_mutex);
  return h;
}

void netPlace(char *out, size_t n) {
  if (!n) return;
  if (!g_mutex) { out[0] = '\0'; return; }
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  strncpy(out, g_place, n - 1);
  out[n - 1] = '\0';
  xSemaphoreGive(g_mutex);
}
