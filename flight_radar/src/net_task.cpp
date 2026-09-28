#include "net_task.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../secrets.h"
#include "config.h"
#include "geo.h"
#include "opensky_client.h"
#include "opensky_parser.h"

namespace {

const uint32_t WIFI_ATTEMPT_MS = 10000;
const int WIFI_ATTEMPTS_BEFORE_DOWN = 3;
const size_t REPORTS_CAPACITY = MAX_AIRCRAFT + 64;

NetStatus g_status = {NetState::WIFI_CONNECTING, 0, 0, 0, -1, 0, 0};
SemaphoreHandle_t g_mutex = nullptr;
AircraftStore *g_store = nullptr;
bool g_have_poll = false;

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

void sleepSeconds(uint32_t s) {
  for (uint32_t left = s; left > 0; --left) {
    setNextPoll(left);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  setNextPoll(0);
}

unsigned freeHeap() { return (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
unsigned freePsram() { return (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

// One 10 s connection attempt. Returns true once connected.
bool wifiAttempt(int &failed_attempts) {
  NetState st = failed_attempts >= WIFI_ATTEMPTS_BEFORE_DOWN ? NetState::WIFI_DOWN : NetState::WIFI_CONNECTING;
  setState(st);
  Serial.printf("[net] wifi: attempt %d heap=%u psram=%u\n", failed_attempts + 1,
                freeHeap(), freePsram());
  WiFi.disconnect(false, false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);
  uint32_t start = millis();
  while (millis() - start < WIFI_ATTEMPT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[net] wifi: connected ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      failed_attempts = 0;
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  failed_attempts++;
  Serial.printf("[net] wifi: attempt failed, status=%d\n", (int)WiFi.status());
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

  char query[128];
  geo::formatBBoxQuery(geo::bbox(geo::makeHome(HOME_LAT, HOME_LON), OBS_RADIUS_M), query, sizeof(query));
  Serial.printf("[net] task started, bbox %s\n", query);

  OpenSkyClient client(OPENSKY_CLIENT_ID, OPENSKY_CLIENT_SECRET);
  WiFi.mode(WIFI_STA);
  int wifi_failed = 0;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiAttempt(wifi_failed);
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
    int code = client.fetchStates(query, &body, &len, &rate);
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
  return epoch + (millis() - at) / 1000;
}
