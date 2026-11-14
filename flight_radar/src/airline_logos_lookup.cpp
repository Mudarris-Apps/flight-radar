// airline_logos_lookup.cpp: callsign -> logo lookup (kept apart from the generated table).
#include "airline_logos.h"
#include <cstring>

bool airlineCodeFromCallsign(const char *cs, char out[4]) {
  if (!cs) return false;
  for (int i = 0; i < 3; ++i)
    if (cs[i] < 'A' || cs[i] > 'Z') return false;
  if (cs[3] < '0' || cs[3] > '9') return false;
  memcpy(out, cs, 3);
  out[3] = '\0';
  return true;
}

int airlineLogoIndex(const char *callsign) {
  char code[4];
  if (!airlineCodeFromCallsign(callsign, code)) return -1;
  size_t lo = 0, hi = AIRLINE_LOGOS_COUNT;   // binary search over [lo, hi)
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int c = strcmp(AIRLINE_LOGOS[mid].code, code);
    if (c == 0) return (int)mid;
    if (c < 0) lo = mid + 1;
    else hi = mid;
  }
  return -1;
}
