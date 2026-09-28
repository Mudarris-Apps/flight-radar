// metadata_table.cpp: STUB, empty table. This is the committed version.
// To show registration, type, model and operator on the detail card, generate
// the full table locally from the OpenSky aircraft database:
//   python3 tools/gen_metadata_table.py <csv path or URL>
// The generated table is OpenSky-derived data under OpenSky's terms of use.
// Never commit it; CI fails if this file is not the stub.
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
