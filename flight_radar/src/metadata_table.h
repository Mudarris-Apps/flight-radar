// metadata_table.h: static aircraft metadata (registration, type, operator) keyed by ICAO24.
#pragma once
#include <stdint.h>
#include <stddef.h>
struct MetadataRow { uint32_t icao24; char reg[8]; char typecode[5]; char model[24]; char op[24]; };
extern const MetadataRow METADATA[]; extern const size_t METADATA_COUNT;   // sorted by icao24
const MetadataRow *metadataLookup(const char *icao24_hex);                 // nullptr when absent
