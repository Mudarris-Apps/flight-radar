#include "aircraft_store.h"
#include <cstring>
#include <cmath>
#include <cstdlib>

#ifdef ARDUINO
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

namespace {

bool validLatLon(float lat, float lon) { return !std::isnan(lat) && !std::isnan(lon); }

// Appends a trail point iff time_position != 0, lat/lon are valid, and
// time_position is strictly newer than the last recorded point's t.
void maybeAppendTrail(Aircraft &ac, float lat, float lon, uint32_t time_position) {
  if (time_position == 0 || !validLatLon(lat, lon)) return;
  if (ac.trail_len > 0) {
    size_t last_idx = (ac.trail_head + TRAIL_CAPACITY - 1) % TRAIL_CAPACITY;
    if (!(time_position > ac.trail[last_idx].t)) return;
  }
  ac.trail[ac.trail_head] = TrailPoint{lat, lon, time_position};
  ac.trail_head = (ac.trail_head + 1) % TRAIL_CAPACITY;
  if (ac.trail_len < TRAIL_CAPACITY) ac.trail_len++;
}

}  // namespace

AircraftStore::AircraftStore(const geo::Home &home, float obs_radius_m)
    : table_(nullptr), home_(home), obs_radius_m_(obs_radius_m), generation_(0) {
#ifdef ARDUINO
  table_ = static_cast<Aircraft *>(heap_caps_malloc(sizeof(Aircraft) * MAX_AIRCRAFT, MALLOC_CAP_SPIRAM));
  std::memset(table_, 0, sizeof(Aircraft) * MAX_AIRCRAFT);
  mutex_ = xSemaphoreCreateMutex();
#else
  table_ = static_cast<Aircraft *>(std::calloc(MAX_AIRCRAFT, sizeof(Aircraft)));
#endif
}

AircraftStore::~AircraftStore() {
#ifdef ARDUINO
  if (table_) heap_caps_free(table_);
  if (mutex_) vSemaphoreDelete(static_cast<SemaphoreHandle_t>(mutex_));
#else
  std::free(table_);
#endif
}

void AircraftStore::lock() const {
#ifdef ARDUINO
  xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY);
#endif
}

void AircraftStore::unlock() const {
#ifdef ARDUINO
  xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
#endif
}

void AircraftStore::applyPoll(const AircraftReport *reports, size_t n, uint32_t poll_time) {
  lock();
  generation_++;

  bool touched[MAX_AIRCRAFT] = {false};

  for (size_t i = 0; i < n; i++) {
    const AircraftReport &r = reports[i];
    if (r.icao24[0] == '\0') continue;

    int idx = -1;
    for (int j = 0; j < MAX_AIRCRAFT; j++) {
      if (table_[j].in_use && std::strcmp(table_[j].last.icao24, r.icao24) == 0) { idx = j; break; }
    }

    bool has_pos = validLatLon(r.lat, r.lon);

    if (!has_pos) {
      if (idx < 0) continue;  // new aircraft without position: dropped
      // Existing aircraft with a null fix keeps its previous last position,
      // but its contact/poll bookkeeping still advances.
      Aircraft &ac = table_[idx];
      ac.last.last_contact = r.last_contact;
      ac.last_seen_poll = poll_time;
      ac.stale = false;
      touched[idx] = true;
      continue;
    }

    if (geo::distanceM(home_, r.lat, r.lon) > obs_radius_m_) continue;  // out of radius: dropped

    if (idx >= 0) {
      Aircraft &ac = table_[idx];
      maybeAppendTrail(ac, r.lat, r.lon, r.time_position);
      ac.last = r;
      ac.last_seen_poll = poll_time;
      ac.stale = false;
      touched[idx] = true;
    } else {
      int free_idx = -1;
      for (int j = 0; j < MAX_AIRCRAFT; j++) {
        if (!table_[j].in_use) { free_idx = j; break; }
      }
      if (free_idx < 0) continue;  // store full: dropped
      Aircraft &ac = table_[free_idx];
      ac = Aircraft{};
      ac.last = r;
      ac.first_seen = poll_time;
      ac.last_seen_poll = poll_time;
      ac.stale = false;
      ac.in_use = true;
      ac.meta = nullptr;
      maybeAppendTrail(ac, r.lat, r.lon, r.time_position);
      touched[free_idx] = true;
    }
  }

  for (int j = 0; j < MAX_AIRCRAFT; j++) {
    if (!table_[j].in_use || touched[j]) continue;
    table_[j].stale = true;
    if (poll_time - table_[j].last_seen_poll >= STALE_REMOVE_S) {
      table_[j] = Aircraft{};  // in_use becomes false; slot free for reuse
    }
  }

  unlock();
}

size_t AircraftStore::count() const {
  lock();
  size_t c = 0;
  for (int j = 0; j < MAX_AIRCRAFT; j++) {
    if (table_[j].in_use) c++;
  }
  unlock();
  return c;
}

const Aircraft *AircraftStore::find(const char *icao24) const {
  lock();
  const Aircraft *found = nullptr;
  for (int j = 0; j < MAX_AIRCRAFT; j++) {
    if (table_[j].in_use && std::strcmp(table_[j].last.icao24, icao24) == 0) { found = &table_[j]; break; }
  }
  unlock();
  return found;
}

size_t AircraftStore::snapshot(Aircraft *out, size_t max) const {
  lock();
  size_t n = 0;
  for (int j = 0; j < MAX_AIRCRAFT && n < max; j++) {
    if (table_[j].in_use) out[n++] = table_[j];
  }
  unlock();
  return n;
}

uint32_t AircraftStore::generation() const { return generation_; }

size_t AircraftStore::trailWindow(const Aircraft &a, uint32_t now, uint32_t window_s, TrailPoint *out, size_t max) {
  size_t len = a.trail_len;
  if (len == 0 || max == 0) return 0;
  uint32_t lower = (window_s <= now) ? (now - window_s) : 0;
  size_t start = (a.trail_head + TRAIL_CAPACITY - len) % TRAIL_CAPACITY;
  size_t n = 0;
  for (size_t k = 0; k < len && n < max; k++) {
    const TrailPoint &p = a.trail[(start + k) % TRAIL_CAPACITY];
    if (p.t >= lower) out[n++] = p;
  }
  return n;
}
