// units.h: SI to aviation unit conversions and NaN-aware formatting (pure, header-only).
#pragma once
#include <cmath>
#include <cstdio>
inline float mToFt(float m) { return m * 3.28084f; }
inline float mpsToKt(float v) { return v * 1.943844f; }
inline float mpsToFpm(float v) { return v * 196.8504f; }
// Writes "Unknown" when NaN, else formatted with the given printf format.
inline void fmtOrUnknown(char *out, size_t n, float v, const char *fmt) { if (std::isnan(v)) std::snprintf(out, n, "Unknown"); else std::snprintf(out, n, fmt, v); }
