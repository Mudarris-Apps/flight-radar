// metadata_table.h: static aircraft metadata (registration, type, model, operator)
// keyed by ICAO24, plus airline names keyed by ICAO airline designator.
//
// The data lives in metadata_table.cpp, which is committed as an empty stub and
// generated locally by tools/gen_metadata_table.py (OpenSky-derived, never
// committed). The lookups live in metadata_lookup.cpp. Pure module: host tested.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct MetadataRow { uint32_t icao24; char reg[8]; char typecode[5]; char model[24]; char op[24]; };

// Fills `out` and returns true when the aircraft is in the table.
bool metadataLookup(const char *icao24_hex, MetadataRow &out);
// ICAO24 of row i (rows are sorted by ICAO24), for tests.
uint32_t metadataIcaoAt(size_t i);
// Airline name for a callsign such as "ANZ123" (Air New Zealand), or nullptr.
const char *airlineNameForCallsign(const char *callsign);

// ---- Generated data (metadata_table.cpp) ----
// Packed rows, METADATA_ROW_BYTES each, sorted by ICAO24:
//   [0..2] ICAO24 big endian, [3..9] registration (NUL padded),
//   [10..11] index into METADATA_TYPES/METADATA_MODELS (little endian, 0 = none),
//   [12..13] index into METADATA_OPS (little endian, 0 = none).
const size_t METADATA_ROW_BYTES = 14;
extern const size_t METADATA_COUNT;
extern const uint8_t METADATA_ROWS[];
extern const size_t METADATA_TYPE_COUNT;
extern const char METADATA_TYPES[][5];
extern const char METADATA_MODELS[][24];
extern const size_t METADATA_OP_COUNT;
extern const char METADATA_OPS[][24];
// Airline designators (sorted) and names.
extern const size_t AIRLINE_COUNT;
extern const char AIRLINE_CODES[][4];
extern const char AIRLINE_NAMES[][24];
