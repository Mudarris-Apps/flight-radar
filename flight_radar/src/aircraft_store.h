#pragma once
#include "aircraft.h"
#include "geo.h"
class AircraftStore {
public:
  AircraftStore(const geo::Home &home, float obs_radius_m);
  ~AircraftStore();
  void   applyPoll(const AircraftReport *reports, size_t n, uint32_t poll_time);
  size_t count() const;
  const Aircraft *find(const char *icao24) const;
  size_t snapshot(Aircraft *out, size_t max) const;   // copies in_use entries
  uint32_t generation() const;                        // increments on every applyPoll
  static size_t trailWindow(const Aircraft &a, uint32_t now, uint32_t window_s, TrailPoint *out, size_t max);  // oldest first
  void lock() const; void unlock() const;             // FreeRTOS mutex on device, no-op on host
private:
  Aircraft *table_; geo::Home home_; float obs_radius_m_; uint32_t generation_;
#ifdef ARDUINO
  void *mutex_;
#endif
};
