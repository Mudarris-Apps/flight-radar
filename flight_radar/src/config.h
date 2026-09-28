#pragma once
#define OBS_RADIUS_M            100000.0f
#define POLL_INTERVAL_S         25
#define POLL_BACKOFF_S          300
#define POLL_ERROR_RETRY_S      30
#define RATE_REMAINING_FLOOR    100
#define MAX_AIRCRAFT            160
#define TRAIL_CAPACITY          96
#define STALE_REMOVE_S          120
#define DR_MAX_EXTRAPOLATION_S  90
#define EASE_MS                 1000
#define ZOOM_MAX_RADIUS_M       105000.0f
#define ZOOM_MIN_RADIUS_M       8000.0f
#define ZOOM_STEP               1.12f
#define ZOOM_EASE_MS            150
#define TRAIL_WINDOW_DEFAULT_S  600
#define OPENSKY_TLS_INSECURE    0
#define UI_TICK_MS              33
#define INPUT_DRAIN_MS          20
#define SCOPE_MARGIN_PX         4
#define LABEL_VIEW_RADIUS_M     40000.0f
#define AIRPORT_LABEL_RADIUS_M  60000.0f
#define TAP_HIT_RADIUS_PX       30
#define BUTTON_BOOT_IGNORE_MS   1000  // drop knob button events this long after knobInputStart (GPIO0 strap glitch on reset)
#define DRAG_THRESHOLD_PX       10    // px of finger travel before a touch becomes a pan (LVGL scroll limit)
#define DISPLAY_ROTATION_DEG    180   // 0 or 180. 180 = flex cable exits to the right; LVGL sw rotation (SH8601 has no mirror-Y/swap-XY)
#define RADAR_DIAG              0     // serial timing diagnostics for the UI tick
