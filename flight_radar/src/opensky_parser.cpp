#include "opensky_parser.h"
#include <ArduinoJson.h>
#include <cmath>
#include <cstring>

namespace {

void copyBounded(const char *src, char *dst, size_t dst_size) {
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t n = strlen(src);
  if (n >= dst_size) n = dst_size - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

void copyCallsign(const char *src, char *dst) {
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t n = strlen(src);
  if (n > 8) n = 8;
  memcpy(dst, src, n);
  dst[n] = '\0';
  while (n > 0 && dst[n - 1] == ' ') {
    dst[--n] = '\0';
  }
}

float floatOrNan(JsonVariantConst v) {
  return v.isNull() ? NAN : v.as<float>();
}

uint32_t u32OrZero(JsonVariantConst v) {
  return v.isNull() ? 0u : v.as<uint32_t>();
}

} // namespace

bool parseOpenSkyStates(const char *json, size_t len, AircraftReport *out, size_t max_out, ParsedStates &info) {
  info.time = 0;
  info.count = 0;
  info.states_null = false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) return false;
  if (!doc.is<JsonObject>()) return false;

  info.time = u32OrZero(doc["time"]);

  JsonVariantConst states = doc["states"];
  if (states.isNull()) {
    info.states_null = true;
    return true;
  }

  JsonArrayConst arr = states.as<JsonArrayConst>();
  size_t count = 0;
  for (JsonVariantConst rowVar : arr) {
    if (count >= max_out) break;
    if (!rowVar.is<JsonArrayConst>()) continue;
    JsonArrayConst row = rowVar.as<JsonArrayConst>();

    AircraftReport &r = out[count];
    memset(&r, 0, sizeof(r));

    copyBounded(row[0].isNull() ? nullptr : row[0].as<const char *>(), r.icao24, sizeof(r.icao24));
    copyCallsign(row[1].isNull() ? nullptr : row[1].as<const char *>(), r.callsign);
    r.time_position = u32OrZero(row[3]);
    r.last_contact = u32OrZero(row[4]);
    r.lon = floatOrNan(row[5]);
    r.lat = floatOrNan(row[6]);
    r.baro_alt_m = floatOrNan(row[7]);
    r.on_ground = row[8].isNull() ? false : row[8].as<bool>();
    r.velocity_mps = floatOrNan(row[9]);
    r.track_deg = floatOrNan(row[10]);
    r.vertical_rate_mps = floatOrNan(row[11]);
    r.geo_alt_m = floatOrNan(row[13]);
    copyBounded(row[14].isNull() ? nullptr : row[14].as<const char *>(), r.squawk, sizeof(r.squawk));
    r.category = row[17].as<uint8_t>();

    count++;
  }

  info.count = count;
  return true;
}
