// metadata_lookup.cpp: lookups over the generated metadata tables (kept apart from
// the generated data in metadata_table.cpp).
#include "metadata_table.h"
#include <cstdlib>
#include <cstring>
#include "airline_logos.h"

namespace {
uint32_t icaoAt(size_t i) {
  const uint8_t *p = METADATA_ROWS + i * METADATA_ROW_BYTES;
  return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}
void copyField(char *dst, size_t dst_size, const char *src, size_t src_max) {
  size_t n = strnlen(src, src_max);
  if (n >= dst_size) n = dst_size - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}
}  // namespace

uint32_t metadataIcaoAt(size_t i) { return i < METADATA_COUNT ? icaoAt(i) : 0; }

bool metadataLookup(const char *icao24_hex, MetadataRow &out) {
  if (!icao24_hex || !icao24_hex[0]) return false;
  char *end = nullptr;
  unsigned long key = std::strtoul(icao24_hex, &end, 16);
  if (!end || *end != '\0' || key > 0xFFFFFFul) return false;
  size_t lo = 0, hi = METADATA_COUNT;   // binary search over [lo, hi)
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    uint32_t v = icaoAt(mid);
    if (v == key) {
      const uint8_t *p = METADATA_ROWS + mid * METADATA_ROW_BYTES;
      uint16_t t = (uint16_t)(p[10] | (p[11] << 8));
      uint16_t o = (uint16_t)(p[12] | (p[13] << 8));
      out = MetadataRow{};
      out.icao24 = v;
      copyField(out.reg, sizeof(out.reg), (const char *)p + 3, 7);
      if (t < METADATA_TYPE_COUNT) {
        copyField(out.typecode, sizeof(out.typecode), METADATA_TYPES[t], sizeof(METADATA_TYPES[t]));
        copyField(out.model, sizeof(out.model), METADATA_MODELS[t], sizeof(METADATA_MODELS[t]));
      }
      if (o < METADATA_OP_COUNT) copyField(out.op, sizeof(out.op), METADATA_OPS[o], sizeof(METADATA_OPS[o]));
      return true;
    }
    if (v < key) lo = mid + 1;
    else hi = mid;
  }
  return false;
}

const char *airlineNameForCallsign(const char *callsign) {
  char code[4];
  if (!airlineCodeFromCallsign(callsign, code)) return nullptr;
  size_t lo = 0, hi = AIRLINE_COUNT;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int c = strncmp(AIRLINE_CODES[mid], code, 4);
    if (c == 0) return AIRLINE_NAMES[mid][0] ? AIRLINE_NAMES[mid] : nullptr;
    if (c < 0) lo = mid + 1;
    else hi = mid;
  }
  return nullptr;
}
