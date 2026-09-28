#pragma once
#include <stddef.h>
#include <stdint.h>
struct Airport { float lat, lon; char ident[5]; uint8_t large; };
extern const Airport AIRPORTS[];
extern const size_t AIRPORTS_COUNT;   // sorted by lat ascending
size_t airportsInBox(float lamin, float lamax, float lomin, float lomax, const Airport **out, size_t max);
