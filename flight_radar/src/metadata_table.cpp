// metadata_table.cpp: STUB, empty table. Task 16 regenerates this file from the OpenSky aircraft database.
#include "metadata_table.h"
#include <cstdlib>

const MetadataRow METADATA[] = {{0, "", "", "", ""}};
const size_t METADATA_COUNT = 0;

const MetadataRow *metadataLookup(const char *icao24_hex) {
  if (!icao24_hex || !icao24_hex[0]) return nullptr;
  char *end = nullptr;
  unsigned long key = std::strtoul(icao24_hex, &end, 16);
  if (!end || *end != '\0' || key > 0xFFFFFFul) return nullptr;
  size_t lo = 0, hi = METADATA_COUNT;   // binary search over [lo, hi)
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    uint32_t v = METADATA[mid].icao24;
    if (v == key) return &METADATA[mid];
    if (v < key) lo = mid + 1;
    else hi = mid;
  }
  return nullptr;
}
