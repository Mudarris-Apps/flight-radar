#pragma once
#include "aircraft.h"
struct ParsedStates { uint32_t time; size_t count; bool states_null; };
// Returns false only on malformed JSON. Fills up to max_out reports.
bool parseOpenSkyStates(const char *json, size_t len, AircraftReport *out, size_t max_out, ParsedStates &info);
