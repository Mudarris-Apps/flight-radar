// airline_logos.h: airline tail logos keyed by ICAO airline code (QFA, VOZ, ...).
// The committed airline_logos.cpp is an empty stub; tools/gen_airline_logos.py
// generates the real table locally (the logos are airline trademarks and must not
// be committed). Pure module: no Arduino or LVGL, so it is host tested.
#pragma once
#include <stddef.h>
#include <stdint.h>

const int AIRLINE_LOGO_W = 22, AIRLINE_LOGO_H = 22;
const size_t AIRLINE_LOGO_BYTES = AIRLINE_LOGO_W * AIRLINE_LOGO_H * 3;   // RGB565 (byte-swapped) + alpha per pixel

struct AirlineLogoRow {
  char code[4];         // ICAO airline designator, NUL terminated
  const uint8_t *data;  // AIRLINE_LOGO_BYTES, LVGL LV_IMG_CF_TRUE_COLOR_ALPHA with LV_COLOR_16_SWAP
};
extern const AirlineLogoRow AIRLINE_LOGOS[];
extern const size_t AIRLINE_LOGOS_COUNT;   // sorted by code

// "QFA123" -> "QFA". Needs three letters then a digit, so registrations such as
// "VHABC" or blank callsigns give false.
bool airlineCodeFromCallsign(const char *callsign, char out[4]);
// Index into AIRLINE_LOGOS for the callsign's airline, or -1.
int  airlineLogoIndex(const char *callsign);
