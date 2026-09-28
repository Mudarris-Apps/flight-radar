# Flight Radar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Live aircraft radar on the Viewe UEDX46460015-MD50ET knob display, fed by OpenSky, with sprites, trails, knob zoom, selection and a detail card.

**Architecture:** Pure C++ modules (geometry, parser, store, dead reckoning, zoom) that build on macOS with clang++ and have host tests; Arduino modules for WiFi/OpenSky (core 0 task) and knob input; LVGL 8 modules for one custom-drawn scope widget plus an `lv_img` sprite pool driven by a 33 ms timer. Everything meets in `ui.cpp`.

**Tech Stack:** arduino-esp32 3.3.11, ESP32_Display_Panel 1.0.4 (lvgl_v8_port example), lvgl 8.4.0, ArduinoJson 7.4.3, ESP32_Knob 0.0.1, ESP32_Button 0.0.1, arduino-cli 1.5.1, clang++ for host tests, python3 for generators.

**Spec:** `docs/superpowers/specs/2026-09-28-flight-radar-design.md` (read it first; this plan argues from it).

## Global Constraints

- Sketch folder is `flight_radar/`; all app sources under `flight_radar/src/` (arduino-cli compiles `src/` recursively). Headers referenced from the `.ino` as `"src/xxx.h"`, from within `src/` as `"xxx.h"`.
- FQBN, always: `esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc`
- arduino-cli binary: `/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli`
- Pure modules (`geo`, `aircraft`, `opensky_parser`, `aircraft_store`, `dead_reckoning`, `zoom_controller`, `airports`) must not include `Arduino.h` or `lvgl.h`. FreeRTOS use only under `#ifdef ARDUINO`.
- Every LVGL call from outside the LVGL task is wrapped in `lvgl_port_lock(-1)` / `lvgl_port_unlock()`. LVGL timers already run inside the LVGL task and need no lock.
- Never commit `flight_radar/secrets.h`. Never print the client secret to serial.
- Numbers in the spec are the defaults in `config.h`; do not hardcode them elsewhere.
- Host tests: `make -C tests` must pass before every commit that touches a pure module.
- Device builds: `tools/build.sh` must succeed before every commit that touches Arduino or LVGL code. Flash with `tools/flash.sh`; if it hangs at "Connecting....", stop and ask the human to hold the knob and replug, then rerun.
- Commit messages end with `AI-Assisted-By: Claude Code`.

## Review Focus

1. OpenSky rows with `null` latitude/longitude or `time_position` (aircraft without a position fix) must be parsed without crashing and must not create trail points or sprites. Test in Task 3 and Task 4.
2. A response with `"states": null` (OpenSky returns this when the box is empty) must be a valid poll that marks everything stale, not a parse error. Test in Task 3.
3. The knob button is GPIO0, the BOOT strap. `Button` must be constructed with pull-up and the ROM's strap behaviour means a press during reset enters the bootloader; the firmware must not reconfigure IO0 as output. Task 10 verifies pressing during normal run never resets the board.
4. Poll epoch versus `millis()`: the board has no clock. All "seconds ago" and dead-reckoning arithmetic must use `last_poll_epoch + (millis() - last_poll_millis)/1000`, never `time(nullptr)`. Test the helper in Task 12.
5. Trail ring buffer at capacity: after 96 points the oldest must be overwritten and `trailWindow` must still return oldest-first without duplicates. Test in Task 4.

---

## Interfaces shared by all tasks

`flight_radar/src/config.h` (created in Task 1, exact contents):

```cpp
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
#define TAP_HIT_RADIUS_PX       22
```

`flight_radar/src/aircraft.h` (Task 3):

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config.h"

struct AircraftReport {
  char icao24[7];
  char callsign[9];
  uint32_t time_position;   // 0 if null
  uint32_t last_contact;
  float lat, lon;           // NAN if null
  float baro_alt_m, geo_alt_m;
  bool on_ground;
  float velocity_mps, track_deg, vertical_rate_mps;
  char squawk[5];
  uint8_t category;
};

struct TrailPoint { float lat, lon; uint32_t t; };

struct MetadataRow;  // defined in metadata_table.h

struct Aircraft {
  AircraftReport last;
  uint32_t first_seen;
  uint32_t last_seen_poll;
  TrailPoint trail[TRAIL_CAPACITY];
  uint16_t trail_head;      // index where the next point is written
  uint16_t trail_len;
  bool stale;
  bool in_use;
  const MetadataRow *meta;
};
```

`flight_radar/src/geo.h` (Task 2):

```cpp
#pragma once
#include <stddef.h>
namespace geo {
struct Home { float lat, lon, cos_lat; };
struct LocalXY { float x, y; };            // metres east, north of home
struct BBox { float lamin, lamax, lomin, lomax; };
struct Projection { float cx, cy, r_px, view_radius_m, pan_x_m, pan_y_m; };

Home    makeHome(float lat, float lon);
LocalXY toLocal(const Home&, float lat, float lon);
float   distanceM(const Home&, float lat, float lon);
BBox    bbox(const Home&, float radius_m);
void    formatBBoxQuery(const BBox&, char *out, size_t n);   // "lamin=-34.8505&lamax=...&lomin=...&lomax=..."
void    project(const Home&, const Projection&, float lat, float lon, float &px, float &py);
bool    insideScope(const Projection&, float px, float py);   // within r_px of (cx,cy)
float   scalePxPerM(const Projection&);
}
```

`flight_radar/src/opensky_parser.h` (Task 3):

```cpp
#pragma once
#include "aircraft.h"
struct ParsedStates { uint32_t time; size_t count; bool states_null; };
// Returns false only on malformed JSON. Fills up to max_out reports.
bool parseOpenSkyStates(const char *json, size_t len, AircraftReport *out, size_t max_out, ParsedStates &info);
```

`flight_radar/src/aircraft_store.h` (Task 4):

```cpp
#pragma once
#include "aircraft.h"
#include "geo.h"
class AircraftStore {
public:
  AircraftStore(const geo::Home &home, float obs_radius_m);
  ~AircraftStore();
  void   applyPoll(const AircraftReport *reports, size_t n, uint32_t poll_time);
  size_t count() const;
  const Aircraft *find(const char *icao24) const;
  size_t snapshot(Aircraft *out, size_t max) const;   // copies in_use entries
  uint32_t generation() const;                        // increments on every applyPoll
  static size_t trailWindow(const Aircraft &a, uint32_t now, uint32_t window_s, TrailPoint *out, size_t max);  // oldest first
  void lock() const; void unlock() const;             // FreeRTOS mutex on device, no-op on host
private:
  Aircraft *table_; geo::Home home_; float obs_radius_m_; uint32_t generation_;
#ifdef ARDUINO
  void *mutex_;
#endif
};
```

`flight_radar/src/dead_reckoning.h` (Task 5):

```cpp
#pragma once
#include "aircraft.h"
struct DisplayState { float lat, lon, track_deg; bool frozen; bool valid; };
DisplayState deadReckon(const AircraftReport &r, uint32_t now_s);
float easeFraction(uint32_t elapsed_ms, uint32_t duration_ms);   // smoothstep, clamped 0..1
float lerpAngleDeg(float from, float to, float t);               // shortest way, result in [0,360)
struct SpriteMotion {
  float cur_lat, cur_lon, cur_track;
  float from_lat, from_lon, from_track;
  uint32_t ease_start_ms; bool easing; bool initialised;
  uint32_t seen_time_position;
  bool frozen;
};
void spriteMotionUpdate(SpriteMotion &m, const AircraftReport &r, uint32_t now_s, uint32_t now_ms);
```

`flight_radar/src/zoom_controller.h` (Task 6):

```cpp
#pragma once
#include <stdint.h>
enum class InputEvent : uint8_t { ROTATE_LEFT, ROTATE_RIGHT, PRESS, LONG_PRESS, TAP, DOUBLE_TAP, DRAG };
class ZoomController {
public:
  ZoomController();
  float viewRadiusM() const;
  float targetRadiusM() const;
  void  zoomIn(); void zoomOut();
  void  handleEvent(InputEvent e);      // ROTATE_RIGHT -> zoomIn, ROTATE_LEFT -> zoomOut, others ignored
  void  tick(uint32_t now_ms);          // eases current toward target over ZOOM_EASE_MS
  void  reset();
  bool  animating() const;
private:
  float current_, target_, from_; uint32_t ease_start_ms_, last_tick_ms_; bool easing_;
};
```

`flight_radar/src/airports.h` (Task 7):

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
struct Airport { float lat, lon; char ident[5]; uint8_t large; };
extern const Airport AIRPORTS[];
extern const size_t AIRPORTS_COUNT;   // sorted by lat ascending
size_t airportsInBox(float lamin, float lamax, float lomin, float lomax, const Airport **out, size_t max);
```

`flight_radar/src/net_task.h` (Task 9):

```cpp
#pragma once
#include <stdint.h>
#include "aircraft_store.h"
enum class NetState : uint8_t { WIFI_CONNECTING, WIFI_DOWN, AUTH_FAILED, RATE_LIMITED, HTTP_ERROR, OK };
struct NetStatus {
  NetState state;
  uint32_t last_poll_epoch;    // OpenSky "time" field of last successful poll
  uint32_t last_poll_millis;   // millis() when that poll was applied
  uint16_t aircraft_count;
  int      rate_remaining;     // -1 unknown
  uint32_t next_poll_in_s;
  uint16_t http_code;
};
void      netTaskStart(AircraftStore *store);
NetStatus netStatusGet();
uint32_t  netNowEpoch();       // last_poll_epoch + (millis()-last_poll_millis)/1000, 0 before first poll
```

`flight_radar/src/knob_input.h` (Task 10):

```cpp
#pragma once
#include "zoom_controller.h"
void knobInputStart();                 // creates queue, ESP_Knob(6,5), Button(GPIO0)
bool knobInputPop(InputEvent &out);    // non-blocking
```

`flight_radar/src/ui.h` (Task 11 onward):

```cpp
#pragma once
#include "aircraft_store.h"
void uiInit(AircraftStore *store);     // call inside lvgl_port_lock
```

---

### Task 1: Scaffold, build scripts, host test harness, minimal sketch

**Files:**
- Create: `flight_radar/flight_radar.ino`, `flight_radar/secrets.example.h`, `flight_radar/src/config.h`, `tools/build.sh`, `tools/flash.sh`, `tools/monitor.sh`, `tests/Makefile`, `tests/test.h`, `tests/test_smoke.cpp`, `README.md`
- Existing (do not edit): `flight_radar/lv_conf.h`, `flight_radar/esp_*_conf.h`, `flight_radar/lvgl_v8_port.*`

**Interfaces:**
- Produces: `config.h` exactly as in the shared block; `tests/test.h` macros `TEST(name)`, `CHECK(expr)`, `CHECK_NEAR(a,b,eps)`, `CHECK_STREQ(a,b)`; each `tests/test_*.cpp` is built as its own binary by the Makefile and run.

- [ ] **Step 1: Write config.h** with the exact contents from "Interfaces shared by all tasks".

- [ ] **Step 2: Write secrets.example.h**

```cpp
#pragma once
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"
#define OPENSKY_CLIENT_ID "your-client-id"
#define OPENSKY_CLIENT_SECRET "your-client-secret"
#define HOME_LAT -33.9461f
#define HOME_LON 151.1772f
```

Then `cp flight_radar/secrets.example.h flight_radar/secrets.h` and fill in the real values from the human (the WiFi SSID and password are not yet known; leave the placeholders and tell the orchestrator that the human must fill them). Confirm `git status` does not list `secrets.h`.

- [ ] **Step 3: Write tools/build.sh, flash.sh, monitor.sh**

```bash
#!/usr/bin/env bash
# tools/build.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc"
mkdir -p "$ROOT/build"
"$CLI" compile --fqbn "$FQBN" --build-path "$ROOT/build" --warnings default "$ROOT/flight_radar" "$@"
```

```bash
#!/usr/bin/env bash
# tools/flash.sh  — usage: tools/flash.sh [port]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc"
PORT="${1:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "no /dev/cu.usbmodem* port found" >&2; exit 1; }
"$CLI" upload --fqbn "$FQBN" --input-dir "$ROOT/build" -p "$PORT" "$ROOT/flight_radar"
```

```bash
#!/usr/bin/env bash
# tools/monitor.sh — usage: tools/monitor.sh [port] [seconds]
set -euo pipefail
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
PORT="${1:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"
SECS="${2:-20}"
timeout "$SECS" "$CLI" monitor -p "$PORT" -c baudrate=115200 || true
```

`chmod +x tools/*.sh`. If `timeout` is missing on macOS, use `gtimeout` from coreutils or a `perl -e 'alarm shift; exec @ARGV'` wrapper; pick one that exists and note it in the README.

- [ ] **Step 4: Write the minimal sketch** (board + LVGL + one label so the build proves the port files work):

```cpp
#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "secrets.h"
#include "src/config.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("[boot] flight_radar, reset reason %d\n", (int)esp_reset_reason());
  Board *board = new Board();
  board->init();
  assert(board->begin());
  lvgl_port_init(board->getLCD(), board->getTouch());
  lvgl_port_lock(-1);
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x05080C), 0);
  lv_obj_t *l = lv_label_create(lv_scr_act());
  lv_label_set_text(l, "RADAR");
  lv_obj_set_style_text_color(l, lv_color_hex(0x2F8F5A), 0);
  lv_obj_center(l);
  lvgl_port_unlock();
}

void loop() { delay(1000); }
```

- [ ] **Step 5: Write the host test harness**

`tests/test.h`:

```cpp
#pragma once
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <functional>
struct TestCase { const char *name; std::function<void()> fn; };
inline std::vector<TestCase> &tests() { static std::vector<TestCase> t; return t; }
inline int &failures() { static int f = 0; return f; }
struct TestRegistrar { TestRegistrar(const char *n, std::function<void()> f) { tests().push_back({n, f}); } };
#define TEST(name) static void name(); static TestRegistrar name##_reg(#name, name); static void name()
#define CHECK(expr) do { if (!(expr)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); failures()++; } } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a=(a), _b=(b); if (std::fabs(_a-_b) > (eps)) { std::printf("  FAIL %s:%d: %s=%g vs %s=%g\n", __FILE__, __LINE__, #a, _a, #b, _b); failures()++; } } while (0)
#define CHECK_STREQ(a, b) do { if (std::strcmp((a),(b)) != 0) { std::printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); failures()++; } } while (0)
inline int runAllTests() {
  for (auto &t : tests()) { std::printf("[ RUN ] %s\n", t.name); t.fn(); }
  std::printf("%zu tests, %d failures\n", tests().size(), failures());
  return failures() ? 1 : 0;
}
#define TEST_MAIN() int main() { return runAllTests(); }
```

`tests/Makefile`:

```make
CXX := clang++
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -DHOST_TEST -I../flight_radar/src -I$(HOME)/Documents/Arduino/libraries/ArduinoJson/src
SRC := $(filter-out ../flight_radar/src/opensky_client.cpp ../flight_radar/src/net_task.cpp ../flight_radar/src/knob_input.cpp ../flight_radar/src/radar_view.cpp ../flight_radar/src/aircraft_layer.cpp ../flight_radar/src/detail_card.cpp ../flight_radar/src/settings_screen.cpp ../flight_radar/src/status_bar.cpp ../flight_radar/src/ui.cpp, $(wildcard ../flight_radar/src/*.cpp))
TESTS := $(wildcard test_*.cpp)
BINS := $(patsubst %.cpp,build/%,$(TESTS))
all: $(BINS)
	@for b in $(BINS); do echo "== $$b"; ./$$b || exit 1; done
build/%: %.cpp $(SRC) test.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@
clean:
	rm -rf build
.PHONY: all clean
```

Add `tests/build/` to `.gitignore`.

`tests/test_smoke.cpp`:

```cpp
#include "test.h"
#include "config.h"
TEST(config_defaults_present) { CHECK(POLL_INTERVAL_S == 25); CHECK(TRAIL_CAPACITY == 96); }
TEST_MAIN()
```

- [ ] **Step 6: Run host tests**: `make -C tests`. Expected: `1 tests, 0 failures`.

- [ ] **Step 7: Build for the device**: `tools/build.sh`. Expected: "Sketch uses ... bytes" and exit 0. First build takes about 65 s.

- [ ] **Step 8: Flash and check serial**: `tools/flash.sh` then `tools/monitor.sh "" 10`. Expected: `[boot] flight_radar, reset reason ...` and the screen shows "RADAR" in green on near-black. If flashing hangs at "Connecting....", stop and report; the human must hold the knob while replugging.

- [ ] **Step 9: Write README.md** covering: what it is, the board, copying `secrets.example.h` to `secrets.h`, `make -C tests`, `tools/build.sh`, `tools/flash.sh`, the knob-hold-and-replug recovery, `tools/monitor.sh`.

- [ ] **Step 10: Commit**

```bash
git add -A && git status --short   # must not show secrets.h
git commit -m "chore: scaffold sketch, build scripts and host test harness

AI-Assisted-By: Claude Code"
```

---

### Task 2: geo module

**Files:**
- Create: `flight_radar/src/geo.h`, `flight_radar/src/geo.cpp`, `tests/test_geo.cpp`

**Interfaces:**
- Produces: exactly the `geo.h` in the shared block.

- [ ] **Step 1: Write failing tests** `tests/test_geo.cpp`:

```cpp
#include "test.h"
#include "geo.h"
#include <cstdio>
static const geo::Home H = geo::makeHome(-33.9461f, 151.1772f);

TEST(local_xy_of_home_is_origin) {
  auto p = geo::toLocal(H, -33.9461f, 151.1772f);
  CHECK_NEAR(p.x, 0, 0.01); CHECK_NEAR(p.y, 0, 0.01);
}
TEST(one_degree_north_is_110574_m) {
  auto p = geo::toLocal(H, -32.9461f, 151.1772f);
  CHECK_NEAR(p.y, 110574, 1); CHECK_NEAR(p.x, 0, 0.01);
}
TEST(distance_to_ysbk_is_about_17_5_km) {
  float d = geo::distanceM(H, -33.9244f, 150.9888f);
  CHECK(d > 17000 && d < 18000);   // 17.56 km by the flat-earth formula
}
TEST(bbox_100km) {
  auto b = geo::bbox(H, 100000);
  CHECK_NEAR(b.lamin, -33.9461 - 0.9044, 0.002);
  CHECK_NEAR(b.lamax, -33.9461 + 0.9044, 0.002);
  CHECK_NEAR(b.lomax - b.lomin, 2 * 100000 / (111320 * std::cos(-33.9461 * M_PI / 180)), 0.002);
  char q[128]; geo::formatBBoxQuery(b, q, sizeof q);
  CHECK(std::strncmp(q, "lamin=-34.8505&lamax=-33.0417&lomin=150.0943&lomax=152.2601", 62) == 0);
}
TEST(projection_centre_and_scale) {
  geo::Projection P{233, 233, 229, 100000, 0, 0};
  float px, py;
  geo::project(H, P, -33.9461f, 151.1772f, px, py);
  CHECK_NEAR(px, 233, 0.01); CHECK_NEAR(py, 233, 0.01);
  geo::project(H, P, -33.9461f + 100000.0f / 110574.0f, 151.1772f, px, py);   // 100 km north
  CHECK_NEAR(px, 233, 0.01); CHECK_NEAR(py, 233 - 229, 0.5);                // up on screen
  CHECK(geo::insideScope(P, 233, 233));
  CHECK(!geo::insideScope(P, 233, 3));
  CHECK_NEAR(geo::scalePxPerM(P), 229.0 / 100000.0, 1e-9);
}
TEST(pan_shifts_projection) {
  geo::Projection P{233, 233, 229, 100000, 10000, 0};    // pan 10 km east
  float px, py;
  geo::project(H, P, -33.9461f, 151.1772f, px, py);
  CHECK_NEAR(px, 233 + 229 * 0.1, 0.01);
}
TEST_MAIN()
```

- [ ] **Step 2: Run** `make -C tests`. Expected: compile error, `geo.h` not found.

- [ ] **Step 3: Implement** `geo.cpp`:

```cpp
#include "geo.h"
#include <cmath>
#include <cstdio>
namespace geo {
static constexpr float M_PER_DEG_LAT = 110574.0f;
static constexpr float M_PER_DEG_LON_EQ = 111320.0f;
Home makeHome(float lat, float lon) { return Home{lat, lon, std::cos(lat * (float)M_PI / 180.0f)}; }
LocalXY toLocal(const Home &h, float lat, float lon) {
  return LocalXY{(lon - h.lon) * h.cos_lat * M_PER_DEG_LON_EQ, (lat - h.lat) * M_PER_DEG_LAT};
}
float distanceM(const Home &h, float lat, float lon) { auto p = toLocal(h, lat, lon); return std::sqrt(p.x * p.x + p.y * p.y); }
BBox bbox(const Home &h, float radius_m) {
  float dlat = radius_m / M_PER_DEG_LAT;
  float dlon = radius_m / (M_PER_DEG_LON_EQ * h.cos_lat);
  return BBox{h.lat - dlat, h.lat + dlat, h.lon - dlon, h.lon + dlon};
}
void formatBBoxQuery(const BBox &b, char *out, size_t n) {
  std::snprintf(out, n, "lamin=%.4f&lamax=%.4f&lomin=%.4f&lomax=%.4f", b.lamin, b.lamax, b.lomin, b.lomax);
}
float scalePxPerM(const Projection &p) { return p.r_px / p.view_radius_m; }
void project(const Home &h, const Projection &p, float lat, float lon, float &px, float &py) {
  auto l = toLocal(h, lat, lon); float s = scalePxPerM(p);
  px = p.cx + (l.x + p.pan_x_m) * s;
  py = p.cy - (l.y + p.pan_y_m) * s;
}
bool insideScope(const Projection &p, float px, float py) {
  float dx = px - p.cx, dy = py - p.cy; return dx * dx + dy * dy <= p.r_px * p.r_px;
}
}
```

Note the sign convention: `pan_x_m` positive moves the world east on screen (content shifts right). Task 11 uses the same convention for drag.

- [ ] **Step 4: Run** `make -C tests`. Expected: all pass. If `M_PI` is undefined, add `#define _USE_MATH_DEFINES` or use `3.14159265f`.

- [ ] **Step 5: Commit** `git add flight_radar/src/geo.* tests/test_geo.cpp && git commit -m "feat(geo): local projection, bounding box and scope maths" -m "AI-Assisted-By: Claude Code"`

---

### Task 3: aircraft.h and OpenSky parser

**Files:**
- Create: `flight_radar/src/aircraft.h`, `flight_radar/src/opensky_parser.h`, `flight_radar/src/opensky_parser.cpp`, `tests/test_opensky_parser.cpp`
- Fixture: `tests/fixtures/opensky_states_sydney.json` (exists; 58 states, time 1790568225)

**Interfaces:**
- Produces: `aircraft.h` and `opensky_parser.h` exactly as in the shared block.

OpenSky row layout (array indices): 0 icao24, 1 callsign, 2 origin_country, 3 time_position, 4 last_contact, 5 longitude, 6 latitude, 7 baro_altitude, 8 on_ground, 9 velocity, 10 true_track, 11 vertical_rate, 12 sensors, 13 geo_altitude, 14 squawk, 15 spi, 16 position_source, 17 category (only with `extended=1`).

- [ ] **Step 1: Write failing tests**:

```cpp
#include "test.h"
#include "opensky_parser.h"
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
static std::string readFixture() {
  std::ifstream f("fixtures/opensky_states_sydney.json"); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
TEST(parses_sydney_fixture) {
  auto s = readFixture(); AircraftReport out[200]; ParsedStates info{};
  CHECK(parseOpenSkyStates(s.data(), s.size(), out, 200, info));
  CHECK(info.count == 58); CHECK(info.time == 1790568225u); CHECK(!info.states_null);
  CHECK_STREQ(out[0].icao24, "7c4e21"); CHECK_STREQ(out[0].callsign, "SYN100");
  CHECK(out[0].on_ground); CHECK(std::isnan(out[0].baro_alt_m)); CHECK(std::isnan(out[0].vertical_rate_mps));
  CHECK_NEAR(out[0].lat, -33.9402, 1e-4); CHECK_NEAR(out[0].lon, 151.1695, 1e-4);
  CHECK_STREQ(out[0].squawk, "");
  CHECK_STREQ(out[1].squawk, "3217"); CHECK_NEAR(out[1].baro_alt_m, 2438.4, 0.01);
}
TEST(null_position_row) {
  const char *j = R"({"time":10,"states":[["abc123",null,"X",null,10,null,null,null,false,null,null,null,null,null,null,false,0,0]]})";
  AircraftReport out[4]; ParsedStates info{};
  CHECK(parseOpenSkyStates(j, strlen(j), out, 4, info));
  CHECK(info.count == 1); CHECK(out[0].time_position == 0); CHECK(std::isnan(out[0].lat)); CHECK_STREQ(out[0].callsign, "");
}
TEST(states_null_is_valid_empty_poll) {
  const char *j = R"({"time":10,"states":null})";
  AircraftReport out[4]; ParsedStates info{};
  CHECK(parseOpenSkyStates(j, strlen(j), out, 4, info));
  CHECK(info.count == 0); CHECK(info.states_null); CHECK(info.time == 10u);
}
TEST(malformed_json_fails) {
  const char *j = "{\"time\":10,\"states\":[[";
  AircraftReport out[4]; ParsedStates info{};
  CHECK(!parseOpenSkyStates(j, strlen(j), out, 4, info));
}
TEST(caps_at_max_out) {
  auto s = readFixture(); AircraftReport out[10]; ParsedStates info{};
  CHECK(parseOpenSkyStates(s.data(), s.size(), out, 10, info)); CHECK(info.count == 10);
}
TEST(short_row_without_category) {
  const char *j = R"({"time":10,"states":[["abc123","QF1     ","AU",9,10,151.0,-33.9,100.0,false,200.0,90.0,1.0,null,120.0,"1200",false,0]]})";
  AircraftReport out[4]; ParsedStates info{};
  CHECK(parseOpenSkyStates(j, strlen(j), out, 4, info)); CHECK(out[0].category == 0); CHECK_STREQ(out[0].callsign, "QF1");
}
TEST_MAIN()
```

The Makefile runs binaries from `tests/`, so the relative fixture path works.

- [ ] **Step 2: Run** `make -C tests`. Expected: compile failure.

- [ ] **Step 3: Implement** with ArduinoJson 7 (`JsonDocument`, `deserializeJson`). Callsign: copy up to 8 chars then right-trim spaces. Floats: `v.isNull() ? NAN : v.as<float>()`. Strings that are null become empty. `states_null = doc["states"].isNull()`. Use `#include <ArduinoJson.h>`; on host it builds with no extra config. The ArduinoJson document lives on the heap; on device the caller will already hold the body in PSRAM, and ArduinoJson 7 allocates its own pool with `malloc`, which the ESP32 core routes to PSRAM for large blocks when `CONFIG_SPIRAM_USE_MALLOC` is on (it is, for the Arduino core with PSRAM enabled). Do not add a custom allocator in this task.

- [ ] **Step 4: Run** `make -C tests`. Expected: pass. If ArduinoJson's include path fails, check `~/Documents/Arduino/libraries/ArduinoJson/src/ArduinoJson.h` exists.

- [ ] **Step 5: Commit** `feat(parser): decode OpenSky states into AircraftReport`.

---

### Task 4: AircraftStore

**Files:**
- Create: `flight_radar/src/aircraft_store.h`, `flight_radar/src/aircraft_store.cpp`, `tests/test_aircraft_store.cpp`

**Interfaces:**
- Consumes: `geo::Home`, `geo::distanceM`, `AircraftReport`, `Aircraft`, `TrailPoint`, `MAX_AIRCRAFT`, `TRAIL_CAPACITY`, `STALE_REMOVE_S`.
- Produces: `aircraft_store.h` exactly as in the shared block.

Rules (from spec): upsert by icao24; append a trail point only when `time_position != 0`, lat/lon not NaN, and `time_position > last recorded point's t`; mark aircraft absent from the poll `stale`; remove when `poll_time - last_seen_poll >= STALE_REMOVE_S`; drop reports with `distanceM > obs_radius_m`; drop reports with NaN position that are new (an existing aircraft receiving a NaN position keeps its previous `last` position but updates `last_contact` and `last_seen_poll`). `generation_` increments per `applyPoll`.

- [ ] **Step 1: Write failing tests**:

```cpp
#include "test.h"
#include "aircraft_store.h"
#include <cstring>
#include <cmath>
static geo::Home H = geo::makeHome(-33.9461f, 151.1772f);
static AircraftReport rep(const char *icao, float lat, float lon, uint32_t tpos) {
  AircraftReport r{}; std::strncpy(r.icao24, icao, 6); r.lat = lat; r.lon = lon; r.time_position = tpos; r.last_contact = tpos;
  r.velocity_mps = 100; r.track_deg = 90; r.baro_alt_m = 1000; r.geo_alt_m = NAN; r.vertical_rate_mps = 0; return r;
}
TEST(upsert_keeps_identity_and_counts) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", -33.9f, 151.0f, 100)};
  s.applyPoll(a, 1, 100); CHECK(s.count() == 1); CHECK(s.generation() == 1);
  a[0].time_position = 125; s.applyPoll(a, 1, 125); CHECK(s.count() == 1); CHECK(s.generation() == 2);
  const Aircraft *x = s.find("7c0001"); CHECK(x != nullptr); CHECK(x->first_seen == 100); CHECK(x->last_seen_poll == 125); CHECK(x->trail_len == 2);
}
TEST(missing_goes_stale_then_removed) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", -33.9f, 151.0f, 100)};
  s.applyPoll(a, 1, 100); s.applyPoll(nullptr, 0, 125);
  CHECK(s.find("7c0001")->stale); CHECK(s.count() == 1);
  s.applyPoll(nullptr, 0, 100 + STALE_REMOVE_S); CHECK(s.find("7c0001") == nullptr); CHECK(s.count() == 0);
}
TEST(trail_appends_only_on_newer_time_position) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", -33.9f, 151.0f, 100)};
  s.applyPoll(a, 1, 100); s.applyPoll(a, 1, 125);            // same time_position
  CHECK(s.find("7c0001")->trail_len == 1);
  a[0].time_position = 0; a[0].lat = NAN; a[0].lon = NAN; s.applyPoll(a, 1, 150);   // null fix
  const Aircraft *x = s.find("7c0001"); CHECK(x->trail_len == 1); CHECK_NEAR(x->last.lat, -33.9, 1e-6); CHECK(x->last_seen_poll == 150); CHECK(!x->stale);
}
TEST(new_aircraft_without_position_is_dropped) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", NAN, NAN, 0)};
  s.applyPoll(a, 1, 100); CHECK(s.count() == 0);
}
TEST(out_of_radius_dropped) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", -36.0f, 151.0f, 100)};   // ~230 km south
  s.applyPoll(a, 1, 100); CHECK(s.count() == 0);
}
TEST(ring_buffer_wraps_and_window_is_oldest_first) {
  AircraftStore s(H, 100000); AircraftReport a[1] = {rep("7c0001", -33.9f, 151.0f, 0)};
  for (uint32_t i = 1; i <= TRAIL_CAPACITY + 10; i++) { a[0].time_position = i * 10; a[0].lat = -33.9f + i * 1e-4f; s.applyPoll(a, 1, i * 10); }
  const Aircraft *x = s.find("7c0001"); CHECK(x->trail_len == TRAIL_CAPACITY);
  TrailPoint out[TRAIL_CAPACITY]; uint32_t now = (TRAIL_CAPACITY + 10) * 10;
  size_t n = AircraftStore::trailWindow(*x, now, 600, out, TRAIL_CAPACITY);
  CHECK(n == 61);                                       // t in [now-600, now] inclusive at 10 s spacing
  for (size_t i = 1; i < n; i++) CHECK(out[i].t > out[i - 1].t);
  CHECK(out[n - 1].t == now);
  size_t all = AircraftStore::trailWindow(*x, now, 1000000, out, TRAIL_CAPACITY); CHECK(all == TRAIL_CAPACITY);
  CHECK(out[0].t == (uint32_t)(10 + 1) * 10 && out[0].t == 110);   // 10 oldest were overwritten
}
TEST(snapshot_copies_in_use_only) {
  AircraftStore s(H, 100000); AircraftReport a[2] = {rep("7c0001", -33.9f, 151.0f, 100), rep("7c0002", -33.8f, 151.1f, 100)};
  s.applyPoll(a, 2, 100); Aircraft out[MAX_AIRCRAFT]; CHECK(s.snapshot(out, MAX_AIRCRAFT) == 2); CHECK(out[0].in_use && out[1].in_use);
}
TEST(store_full_drops_extra) {
  AircraftStore s(H, 100000); AircraftReport a[MAX_AIRCRAFT + 5];
  for (int i = 0; i < MAX_AIRCRAFT + 5; i++) { char id[7]; std::snprintf(id, 7, "%06x", 0x7c0000 + i); a[i] = rep(id, -33.9f, 151.0f, 100); }
  s.applyPoll(a, MAX_AIRCRAFT + 5, 100); CHECK(s.count() == MAX_AIRCRAFT);
}
TEST_MAIN()
```

Window semantics: `trailWindow` returns points with `t >= now - window_s`, oldest first (inclusive lower bound), which is why the wrap test expects 61 points.

- [ ] **Step 2: Run** `make -C tests`. Expected: compile failure.

- [ ] **Step 3: Implement.** Allocate `table_` with `heap_caps_malloc(sizeof(Aircraft) * MAX_AIRCRAFT, MALLOC_CAP_SPIRAM)` under `#ifdef ARDUINO` (include `esp_heap_caps.h`), else `calloc`. Zero it. Mutex: `xSemaphoreCreateMutex()` under `#ifdef ARDUINO`; `lock()`/`unlock()` take/give it, no-ops on host. `applyPoll` takes the lock internally; `snapshot`, `find`, `count` also take it (recursive locking is not needed because the public methods do not call each other). Trail write: `trail[trail_head] = p; trail_head = (trail_head + 1) % TRAIL_CAPACITY; if (trail_len < TRAIL_CAPACITY) trail_len++;`. Oldest index: `(trail_head + TRAIL_CAPACITY - trail_len) % TRAIL_CAPACITY`.

- [ ] **Step 4: Run** `make -C tests`. Expected: pass.

- [ ] **Step 5: Commit** `feat(store): aircraft table with trails, staleness and snapshots`.

---

### Task 5: Dead reckoning and sprite easing

**Files:**
- Create: `flight_radar/src/dead_reckoning.h`, `flight_radar/src/dead_reckoning.cpp`, `tests/test_dead_reckoning.cpp`

**Interfaces:**
- Consumes: `AircraftReport`, `DR_MAX_EXTRAPOLATION_S`, `EASE_MS`.
- Produces: `dead_reckoning.h` exactly as in the shared block.

Rules: `deadReckon` returns `valid=false` when lat/lon NaN. If `on_ground`, velocity NaN, or track NaN: position = reported, `frozen=false`. Else `dt = min(now_s - time_position, DR_MAX_EXTRAPOLATION_S)` (0 if now < time_position); move `velocity*dt` metres along `track_deg` (0 = north, 90 = east) using 110574 m per degree lat and `111320*cos(lat)` per degree lon; `frozen = (now_s - time_position) > DR_MAX_EXTRAPOLATION_S`. `spriteMotionUpdate`: compute `dr = deadReckon(r, now_s)`; if `!initialised` set cur = dr, initialised = true; else if `r.time_position != seen_time_position` start an ease: from = cur, ease_start_ms = now_ms, easing = true; while easing, `t = easeFraction(now_ms - ease_start_ms, EASE_MS)`, cur = lerp(from, dr, t) (track via `lerpAngleDeg`), easing ends at t >= 1. When not easing, cur = dr. Always update `seen_time_position = r.time_position`, `frozen = dr.frozen`.

- [ ] **Step 1: Write failing tests**:

```cpp
#include "test.h"
#include "dead_reckoning.h"
#include <cmath>
#include <cstring>
static AircraftReport rep(float lat, float lon, uint32_t tpos, float v, float trk, bool ground=false) {
  AircraftReport r{}; std::strcpy(r.icao24, "7c0001"); r.lat = lat; r.lon = lon; r.time_position = tpos; r.last_contact = tpos;
  r.velocity_mps = v; r.track_deg = trk; r.on_ground = ground; r.vertical_rate_mps = 0; r.baro_alt_m = 1000; r.geo_alt_m = NAN; return r;
}
TEST(sixty_seconds_east_at_250) {
  auto d = deadReckon(rep(-33.9f, 151.0f, 1000, 250, 90), 1060);
  CHECK(d.valid); CHECK(!d.frozen); CHECK_NEAR(d.lat, -33.9, 1e-5);
  float dlon = 15000.0f / (111320.0f * std::cos(-33.9f * (float)M_PI / 180)); CHECK_NEAR(d.lon, 151.0 + dlon, 2e-4);
}
TEST(caps_extrapolation_and_flags_frozen) {
  auto d = deadReckon(rep(-33.9f, 151.0f, 1000, 250, 0), 1000 + DR_MAX_EXTRAPOLATION_S + 300);
  CHECK(d.frozen); CHECK_NEAR(d.lat, -33.9 + 250.0 * DR_MAX_EXTRAPOLATION_S / 110574.0, 1e-5);
}
TEST(ground_and_nan_do_not_move) {
  auto g = deadReckon(rep(-33.9f, 151.0f, 1000, 10, 90, true), 1060); CHECK_NEAR(g.lon, 151.0, 1e-7);
  auto n = deadReckon(rep(-33.9f, 151.0f, 1000, NAN, 90), 1060); CHECK_NEAR(n.lon, 151.0, 1e-7);
  auto bad = deadReckon(rep(NAN, NAN, 1000, 10, 90), 1060); CHECK(!bad.valid);
}
TEST(ease_fraction_and_angle) {
  CHECK_NEAR(easeFraction(0, 1000), 0, 1e-6); CHECK_NEAR(easeFraction(500, 1000), 0.5, 1e-6); CHECK_NEAR(easeFraction(2000, 1000), 1, 1e-6);
  CHECK_NEAR(lerpAngleDeg(350, 10, 0.5), 0, 1e-4); CHECK_NEAR(lerpAngleDeg(10, 350, 0.5), 0, 1e-4); CHECK_NEAR(lerpAngleDeg(0, 180, 0.25), 45, 1e-4);
}
TEST(sprite_motion_eases_to_new_report) {
  SpriteMotion m{}; auto r1 = rep(-33.9f, 151.0f, 1000, 0, 0);
  spriteMotionUpdate(m, r1, 1000, 0); CHECK(m.initialised); CHECK_NEAR(m.cur_lat, -33.9, 1e-6);
  auto r2 = rep(-33.8f, 151.0f, 1025, 0, 0);
  spriteMotionUpdate(m, r2, 1025, 10000); CHECK(m.easing); CHECK_NEAR(m.cur_lat, -33.9, 1e-6);
  spriteMotionUpdate(m, r2, 1025, 10500); CHECK_NEAR(m.cur_lat, -33.85, 1e-4);
  spriteMotionUpdate(m, r2, 1026, 10000 + EASE_MS); CHECK(!m.easing); CHECK_NEAR(m.cur_lat, -33.8, 1e-6);
}
TEST_MAIN()
```

- [ ] **Step 2: Run** `make -C tests`. Expected: compile failure.
- [ ] **Step 3: Implement** per the rules above. `easeFraction`: `t = clamp(elapsed/duration, 0, 1); return t*t*(3-2*t)`.
- [ ] **Step 4: Run** `make -C tests`. Expected: pass.
- [ ] **Step 5: Commit** `feat(motion): dead reckoning and eased sprite motion`.

---

### Task 6: ZoomController

**Files:**
- Create: `flight_radar/src/zoom_controller.h`, `flight_radar/src/zoom_controller.cpp`, `tests/test_zoom_controller.cpp`

**Interfaces:**
- Produces: `zoom_controller.h` exactly as in the shared block.

- [ ] **Step 1: Write failing tests**:

```cpp
#include "test.h"
#include "zoom_controller.h"
#include "config.h"
TEST(starts_at_max) { ZoomController z; CHECK_NEAR(z.viewRadiusM(), ZOOM_MAX_RADIUS_M, 1); CHECK_NEAR(z.targetRadiusM(), ZOOM_MAX_RADIUS_M, 1); }
TEST(zoom_in_divides_by_step_and_clamps) {
  ZoomController z; z.zoomIn(); CHECK_NEAR(z.targetRadiusM(), ZOOM_MAX_RADIUS_M / ZOOM_STEP, 1);
  for (int i = 0; i < 100; i++) z.zoomIn(); CHECK_NEAR(z.targetRadiusM(), ZOOM_MIN_RADIUS_M, 1);
  for (int i = 0; i < 100; i++) z.zoomOut(); CHECK_NEAR(z.targetRadiusM(), ZOOM_MAX_RADIUS_M, 1);
}
TEST(events_map) {
  ZoomController z; z.handleEvent(InputEvent::ROTATE_RIGHT); CHECK(z.targetRadiusM() < ZOOM_MAX_RADIUS_M);
  float t = z.targetRadiusM(); z.handleEvent(InputEvent::PRESS); CHECK_NEAR(z.targetRadiusM(), t, 0.01);
  z.handleEvent(InputEvent::ROTATE_LEFT); CHECK_NEAR(z.targetRadiusM(), ZOOM_MAX_RADIUS_M, 1);
}
TEST(easing_converges_in_ease_ms) {
  ZoomController z; z.tick(1000); z.zoomIn(); z.tick(1000);
  CHECK(z.animating()); CHECK_NEAR(z.viewRadiusM(), ZOOM_MAX_RADIUS_M, 1);
  z.tick(1000 + ZOOM_EASE_MS / 2); CHECK(z.viewRadiusM() < ZOOM_MAX_RADIUS_M && z.viewRadiusM() > z.targetRadiusM());
  z.tick(1000 + ZOOM_EASE_MS); CHECK(!z.animating()); CHECK_NEAR(z.viewRadiusM(), z.targetRadiusM(), 0.01);
}
TEST(full_range_is_about_23_detents) {
  ZoomController z; int n = 0; while (z.targetRadiusM() > ZOOM_MIN_RADIUS_M + 1 && n < 100) { z.zoomIn(); n++; } CHECK(n >= 22 && n <= 24);
}
TEST(reset_returns_to_max) { ZoomController z; z.zoomIn(); z.zoomIn(); z.reset(); CHECK_NEAR(z.targetRadiusM(), ZOOM_MAX_RADIUS_M, 1); }
TEST_MAIN()
```

- [ ] **Step 2: Run** `make -C tests`. Expected: compile failure.
- [ ] **Step 3: Implement.** `zoomIn`: `from_ = current_; target_ = max(target_/ZOOM_STEP, MIN); ease_start_ms_ = last_tick_ms_; easing_ = true`. `last_tick_ms_` (set in `tick`) is used so a zoom call between ticks starts its ease from the most recent tick time. `tick`: if easing, `t = smoothstep((now-start)/ZOOM_EASE_MS)`, `current_ = from_ + (target_-from_)*t`, and when `t>=1` set `current_=target_`, `easing_=false`. Interpolate in log space (`exp(lerp(log from, log target))`) so zooming feels linear.
- [ ] **Step 4: Run** `make -C tests`. Expected: pass.
- [ ] **Step 5: Commit** `feat(zoom): knob-driven zoom controller with eased view radius`.

---

### Task 7: Airports table and lookup

**Files:**
- Create: `tools/gen_airports.py`, `flight_radar/src/airports.h`, `flight_radar/src/airports_data.cpp` (generated), `flight_radar/src/airports.cpp`, `tests/test_airports.cpp`

**Interfaces:**
- Produces: `airports.h` exactly as in the shared block. `AIRPORTS` sorted by `lat` ascending, `AIRPORTS_COUNT` about 5300.

- [ ] **Step 1: Write the generator** `tools/gen_airports.py`:

```python
#!/usr/bin/env python3
"""Generate flight_radar/src/airports_data.cpp from OurAirports airports.csv.
Usage: tools/gen_airports.py [path-to-airports.csv]   (downloads if omitted)"""
import csv, io, sys, urllib.request, pathlib
URL = "https://davidmegginson.github.io/ourairports-data/airports.csv"
KEEP = {"large_airport": 1, "medium_airport": 0}
def load(path):
    if path: return open(path, newline="", encoding="utf-8").read()
    return urllib.request.urlopen(URL, timeout=60).read().decode("utf-8")
def main():
    text = load(sys.argv[1] if len(sys.argv) > 1 else None)
    rows = []
    for r in csv.DictReader(io.StringIO(text)):
        if r["type"] not in KEEP: continue
        ident = (r["icao_code"] or r["ident"])[:4]
        if not ident or not ident.isalnum(): continue
        rows.append((float(r["latitude_deg"]), float(r["longitude_deg"]), ident.upper(), KEEP[r["type"]]))
    rows.sort()
    out = pathlib.Path(__file__).resolve().parent.parent / "flight_radar" / "src" / "airports_data.cpp"
    with open(out, "w") as f:
        f.write("// Generated by tools/gen_airports.py from OurAirports. Do not edit.\n#include \"airports.h\"\n")
        f.write("const Airport AIRPORTS[] = {\n")
        for lat, lon, ident, large in rows:
            f.write(f'  {{{lat:.5f}f, {lon:.5f}f, "{ident}", {large}}},\n')
        f.write("};\n")
        f.write(f"const size_t AIRPORTS_COUNT = {len(rows)};\n")
    print(f"wrote {len(rows)} airports to {out}")
if __name__ == "__main__": main()
```

Run it: `python3 tools/gen_airports.py` (the CSV is also cached at the scratchpad path the orchestrator supplies; if the download fails, ask for that path). Expected: about 5280 rows.

- [ ] **Step 2: Write failing test** `tests/test_airports.cpp`:

```cpp
#include "test.h"
#include "airports.h"
#include <cstring>
TEST(sorted_by_lat) { for (size_t i = 1; i < AIRPORTS_COUNT; i++) CHECK(AIRPORTS[i].lat >= AIRPORTS[i - 1].lat); CHECK(AIRPORTS_COUNT > 5000); }
TEST(sydney_box_contains_yssy_and_ysbk) {
  const Airport *out[64]; size_t n = airportsInBox(-34.82f, -33.01f, 149.95f, 152.12f, out, 64);
  bool yssy = false, ysbk = false; for (size_t i = 0; i < n; i++) { if (!std::strcmp(out[i]->ident, "YSSY")) yssy = out[i]->large == 1; if (!std::strcmp(out[i]->ident, "YSBK")) ysbk = true; }
  CHECK(yssy); CHECK(ysbk); CHECK(n >= 5 && n <= 20);
}
TEST(empty_box) { const Airport *out[4]; CHECK(airportsInBox(-89.9f, -89.8f, 0, 1, out, 4) == 0); }
TEST_MAIN()
```

- [ ] **Step 3: Implement** `airports.cpp`: binary search the first index with `lat >= lamin`, scan while `lat <= lamax`, filter by lon, fill `out` up to `max`.
- [ ] **Step 4: Run** `make -C tests`. Expected: pass. Note the Makefile compiles every `src/*.cpp` into every test binary; `airports_data.cpp` is 70 KB of source and is fine.
- [ ] **Step 5: Commit** `feat(airports): embedded large and medium airports with box lookup` (commit the generated file).

---

### Task 8: Airplane sprite

**Files:**
- Create: `tools/gen_plane_sprite.py`, `flight_radar/src/sprites/plane_24.c`, `flight_radar/src/sprites/plane_24.h`

**Interfaces:**
- Produces: `extern const lv_img_dsc_t plane_24;` declared in `plane_24.h` (guarded by `#include <lvgl.h>`), 24 x 24, `LV_IMG_CF_ALPHA_8BIT`, nose pointing up (towards y = 0).

- [ ] **Step 1: Write the generator** (pure Python, no Pillow). Define the silhouette as polygons in a 24 x 24 coordinate space: fuselage `[(11,1),(13,1),(13.6,6),(13.6,18),(13,22),(11,22),(10.4,18),(10.4,6)]`, wings `[(10.6,8),(13.4,8),(23,14.5),(23,16.2),(13.4,13.5),(10.6,13.5),(1,16.2),(1,14.5)]`, tailplane `[(10.7,18.5),(13.3,18.5),(17.5,21.6),(17.5,22.8),(13.3,21.4),(10.7,21.4),(6.5,22.8),(6.5,21.6)]`, fin `[(11.6,16),(12.4,16),(12.4,22.5),(11.6,22.5)]`. Rasterise with 8 x 8 supersampling: for each pixel, count sub-samples inside any polygon (even-odd point-in-polygon), alpha = round(255 * count / 64). Emit:

```c
#include "plane_24.h"
static const uint8_t plane_24_map[] = { /* 576 bytes */ };
const lv_img_dsc_t plane_24 = {
  .header = { .cf = LV_IMG_CF_ALPHA_8BIT, .always_zero = 0, .reserved = 0, .w = 24, .h = 24 },
  .data_size = 576,
  .data = plane_24_map,
};
```

`plane_24.h`: `#pragma once` / `#include <lvgl.h>` / `#ifdef __cplusplus extern "C" { #endif` / `extern const lv_img_dsc_t plane_24;` / close.

Also print an ASCII preview (`#` for alpha > 128, `+` for > 32, `.` otherwise) so the silhouette can be eyeballed; it must read as a plane with the nose at the top.

- [ ] **Step 2: Run** `python3 tools/gen_plane_sprite.py` and check the preview.
- [ ] **Step 3: Build** `tools/build.sh` (the `.c` is compiled; LVGL 8 initialiser field order must match `lv_img_dsc_t`; if the designated initialiser fails under C, write positional fields).
- [ ] **Step 4: Commit** `feat(sprite): top-down airplane A8 sprite and generator`.

---

### Task 9: OpenSky client and network task (device)

**Files:**
- Create: `flight_radar/src/opensky_client.h`, `flight_radar/src/opensky_client.cpp`, `flight_radar/src/net_task.h`, `flight_radar/src/net_task.cpp`, `flight_radar/src/isrg_root_x1.h`
- Modify: `flight_radar/flight_radar.ino` (start the task; print status in `loop()`)

**Interfaces:**
- Consumes: `parseOpenSkyStates`, `AircraftStore::applyPoll`, `geo::bbox`, `geo::formatBBoxQuery`, secrets macros, config macros.
- Produces: `net_task.h` exactly as in the shared block.

`opensky_client.h`:

```cpp
#pragma once
#include <Arduino.h>
class OpenSkyClient {
public:
  OpenSkyClient(const char *client_id, const char *client_secret);
  bool ensureToken();                       // fetch when missing or within 120 s of expiry; true if a token is held
  // GET states; returns HTTP code (200 ok) or negative HTTPClient error. On 200, *body is heap memory (free()) of *len bytes.
  int  fetchStates(const char *bbox_query, char **body, size_t *len, int *rate_remaining);
  void invalidateToken();
private:
  const char *id_, *secret_; String token_; uint32_t token_expiry_ms_ = 0;
};
```

- [ ] **Step 1: isrg_root_x1.h**: the ISRG Root X1 PEM (valid to 2035) as `static const char ISRG_ROOT_X1_PEM[] PROGMEM = "-----BEGIN CERTIFICATE-----\n" ... ;`. Copy it from `https://letsencrypt.org/certs/isrgrootx1.pem` (fetch with curl and paste; verify the first line after BEGIN starts with `MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw`).

- [ ] **Step 2: Implement OpenSkyClient** with `WiFiClientSecure` + `HTTPClient`. Token: `http.begin(client, "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"); http.addHeader("Content-Type","application/x-www-form-urlencoded"); int code = http.POST("grant_type=client_credentials&client_id=" + id + "&client_secret=" + secret);` parse `access_token` and `expires_in` with ArduinoJson (filter to those two keys), `token_expiry_ms_ = millis() + (expires_in - 120) * 1000`. States: `http.begin(client, String("https://opensky-network.org/api/states/all?") + bbox_query + "&extended=1"); http.addHeader("Authorization", "Bearer " + token_); const char *hdrs[] = {"X-Rate-Limit-Remaining"}; http.collectHeaders(hdrs, 1); code = http.GET();` then read the body via `http.getStream()` into a `heap_caps_malloc(size+1, MALLOC_CAP_SPIRAM)` buffer when `Content-Length` is known, otherwise grow in 8 KB chunks (OpenSky sends Content-Length). `client.setCACert(ISRG_ROOT_X1_PEM)` unless `OPENSKY_TLS_INSECURE`, then `client.setInsecure()`. Set `http.setTimeout(15000)`. Never log the secret.

- [ ] **Step 3: Implement net_task.cpp**: a `static NetStatus g_status` behind a mutex; `netStatusGet()` copies it. Task body (pinned core 0, stack 24576, priority 1):

```
loop:
  if WiFi not connected: state=WIFI_CONNECTING (WIFI_DOWN after 3 failed 10 s attempts), WiFi.begin(WIFI_SSID, WIFI_PASS) once per attempt, wait up to 10 s, continue
  if !client.ensureToken(): state=AUTH_FAILED, sleep POLL_ERROR_RETRY_S, continue
  code = client.fetchStates(query, &body, &len, &rate)
  if code == 200: parse into a PSRAM array of MAX_AIRCRAFT+64 AircraftReport (allocate once); if parse ok: store->applyPoll(reports, n, info.time); status OK, last_poll_epoch=info.time, last_poll_millis=millis(), aircraft_count=store->count(), rate_remaining=rate; wait = (rate >= 0 && rate < RATE_REMAINING_FLOOR) ? POLL_BACKOFF_S : POLL_INTERVAL_S
              else: state=HTTP_ERROR, wait=POLL_ERROR_RETRY_S
  elif code == 401: client.invalidateToken(); state=AUTH_FAILED; wait=1 (re-auth next loop, but only retry auth once per minute)
  elif code == 429: state=RATE_LIMITED; wait=POLL_BACKOFF_S
  else: state=HTTP_ERROR; http_code=code; wait=POLL_ERROR_RETRY_S
  free(body); log one line: "[net] http=%d rate=%d aircraft=%u wait=%us heap=%u psram=%u"
  sleep wait seconds in 1 s steps, updating next_poll_in_s
```

`netNowEpoch()` returns 0 until the first successful poll.

- [ ] **Step 4: Wire the .ino**: after LVGL init, `static AircraftStore store(geo::makeHome(HOME_LAT, HOME_LON), OBS_RADIUS_M); netTaskStart(&store);`. In `loop()`, every 5 s print `netStatusGet()` fields and `store.count()`.

- [ ] **Step 5: Build and flash**: `tools/build.sh && tools/flash.sh`. Then `tools/monitor.sh "" 90`. Expected within 90 s: WiFi connected with an IP, `[net] http=200 rate=39xx aircraft=NN wait=25s`. The human must have filled `secrets.h` with the WiFi credentials first; if the log shows `WIFI_CONNECTING` forever, stop and report. If TLS fails with the pinned root (`connection refused` or `-1` right after connect), rebuild with `OPENSKY_TLS_INSECURE 1` to prove the rest works, report the difference, and leave the flag at 0 in the commit.

- [ ] **Step 6: Commit** `feat(net): OpenSky client credentials flow and polling task`.

---

### Task 10: Knob and button input

**Files:**
- Create: `flight_radar/src/knob_input.h`, `flight_radar/src/knob_input.cpp`
- Modify: `flight_radar/flight_radar.ino` (start input; log events in `loop()` for this task only)

**Interfaces:**
- Produces: `knob_input.h` exactly as in the shared block. Events: rotation → `ROTATE_LEFT`/`ROTATE_RIGHT` (one per detent), single click → `PRESS`, long press start → `LONG_PRESS`.

- [ ] **Step 1: Implement.** `static QueueHandle_t q = xQueueCreate(32, sizeof(InputEvent));` `ESP_Knob *knob = new ESP_Knob(6, 5); knob->begin(); knob->attachLeftEventCallback([](int, void*){ push(ROTATE_LEFT); }); knob->attachRightEventCallback(... ROTATE_RIGHT)`. Button: `Button *btn = new Button(GPIO_NUM_0, true, 0, 0, 0, 0);` check the constructor in `ESP32_Button/src/Button.h` and its `original/` example for the GPIO form (pullup `true`, ADC channel unused). `btn->attachSingleClickEventCb(cb, nullptr)` → PRESS, `attachLongPressStartEventCb` → LONG_PRESS. Callbacks run in the library's task context; use `xQueueSend` with zero wait. `knobInputPop` uses `xQueueReceive(q, &out, 0)`.

- [ ] **Step 2: Wire the .ino** to call `knobInputStart()` and, in `loop()`, drain and print each event name.

- [ ] **Step 3: Build, flash, verify on the board** with `tools/monitor.sh "" 40`: ask the human to turn the knob clockwise five detents, anticlockwise five, short-press, long-press. Expected: 5 × ROTATE_RIGHT, 5 × ROTATE_LEFT, PRESS, LONG_PRESS, no reboot. If clockwise yields ROTATE_LEFT, call `knob->invertDirection()` before `begin()`. If a press reboots the board, the pull-up is not configured; fix the constructor arguments.

- [ ] **Step 4: Commit** `feat(input): knob rotation and button events on a queue`.

---

### Task 11: Radar scope widget, status bar and knob zoom

**Files:**
- Create: `flight_radar/src/radar_view.h`, `flight_radar/src/radar_view.cpp`, `flight_radar/src/status_bar.h`, `flight_radar/src/status_bar.cpp`, `flight_radar/src/ui.h`, `flight_radar/src/ui.cpp`, `flight_radar/src/ui_state.h`
- Modify: `flight_radar/flight_radar.ino` (replace the label with `uiInit(&store)`; remove the event printing from Task 10)

**Interfaces:**
- Consumes: `geo::*`, `ZoomController`, `knobInputPop`, `netStatusGet`, `netNowEpoch`, `airportsInBox`, `AircraftStore::snapshot/generation/trailWindow`.
- Produces:

```cpp
// ui_state.h
#pragma once
#include "aircraft.h"
#include "geo.h"
#include "zoom_controller.h"
#include "dead_reckoning.h"
struct UiState {
  geo::Home home;
  ZoomController zoom;
  float pan_x_m = 0, pan_y_m = 0;
  char selected_icao24[7] = {0};
  uint32_t trail_window_s = TRAIL_WINDOW_DEFAULT_S;
  Aircraft *snap;              // PSRAM array MAX_AIRCRAFT
  SpriteMotion *motion;        // parallel to snap
  size_t snap_n = 0;
  uint32_t snap_generation = 0;
  bool card_open = false;
  bool settings_open = false;
};
// radar_view.h
#pragma once
#include <lvgl.h>
#include "ui_state.h"
lv_obj_t *radarViewCreate(lv_obj_t *parent, UiState *st);   // full-screen object; reads st on every draw
void      radarViewInvalidate(lv_obj_t *rv);
geo::Projection radarViewProjection(lv_obj_t *rv);           // current cx, cy, r_px, view radius, pan
// status_bar.h
#pragma once
#include <lvgl.h>
lv_obj_t *statusBarCreate(lv_obj_t *parent);
void      statusBarUpdate(lv_obj_t *bar);   // reads netStatusGet(), formats per spec
```

- [ ] **Step 1: radar_view.cpp.** Create a plain `lv_obj` sized to the display, no scrollbars, bg `#05080C`, radius 0, and `lv_obj_add_event_cb(obj, draw_cb, LV_EVENT_DRAW_MAIN, st)`. Store `st` as user data. In `draw_cb`, `lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e)`; compute `Projection P{w/2, h/2, min(w,h)/2 - SCOPE_MARGIN_PX, st->zoom.viewRadiusM(), st->pan_x_m, st->pan_y_m}` where w/h come from `lv_obj_get_width/height`, and draw in this order (all LVGL 8.4 draw API: `lv_draw_arc_dsc_t` + `lv_draw_arc(ctx, &dsc, &center, radius, 0, 360)`, `lv_draw_line_dsc_t` + `lv_draw_line(ctx, &dsc, &p1, &p2)`, `lv_draw_label_dsc_t` + `lv_draw_label(ctx, &dsc, &coords, text, NULL)`, `lv_draw_rect_dsc_t` + `lv_draw_rect(ctx, &dsc, &coords)`):
  1. Ring spacing: choose the largest `s` in {2,5,10,20,25,50} km such that `floor(view_radius/s) >= 3`, else 2 km. For each `k*s <= view_radius` draw a 1 px arc `#1B3A2A`, and a label `"%d km"` in `lv_font_montserrat_12` `#3E7A55` at `(cx, cy - r_k - 14)`.
  2. Observation ring at `OBS_RADIUS_M`: 2 px arc `#2F8F5A` (skip when its radius in px exceeds `r_px + 2`).
  3. Compass ticks: every 30°, a 6 px line from `r_px - 8` to `r_px - 2`, `#2F8F5A`; label `N` at the top (`cx - 5, cy - r_px + 8`).
  4. Airports: `airportsInBox` over the bbox of `view_radius_m * 1.2`, project each, skip if not `insideScope`, draw a 5 px hollow square (`lv_draw_rect` with `bg_opa = 0`, `border_width = 1`, `border_color = #5AA0FF`), and when `view_radius_m <= AIRPORT_LABEL_RADIUS_M` a 12 px label of `ident` at `(px + 6, py - 6)` in `#5AA0FF`.
  5. Trails: for every `st->snap[i]` (skip if `!in_use`): `n = AircraftStore::trailWindow(a, netNowEpoch(), st->trail_window_s, pts, TRAIL_CAPACITY)`; if `n >= 2`, project all points, then for each segment `j` set `opa = 51 + 204 * (pts[j+1].t - (now - window)) / window` clamped 51..255, width 2, colour `#E0A030`; if `a.last.icao24 == st->selected_icao24` width 3 colour `#FFFFFF`. Skip a segment when both ends are outside the scope. Then the leader: from the newest point to `(st->motion[i].cur_lat, cur_lon)` at full opacity, width 1.
  6. Home marker: filled 6 px circle (`lv_draw_arc` with width = radius) `#FFFFFF`, then a 1 px `#2F8F5A` arc at radius 5.
  Because `radar_view` is beneath the sprites, the sprite objects (Task 12) are children of the same parent created after it.

- [ ] **Step 2: status_bar.cpp.** A label at `(cx - 100, 28)` width 200, centred text, `lv_font_montserrat_12`, `#3E7A55`. `statusBarUpdate` formats: OK → `"%u aircraft · %us ago"` with `ago = netNowEpoch() - last_poll_epoch`; the other states map to the spec strings; for HTTP_ERROR include the code when non-zero.

- [ ] **Step 3: ui.cpp.** `uiInit(store)`: allocate `st.snap` and `st.motion` in PSRAM (`heap_caps_calloc(MAX_AIRCRAFT, sizeof..., MALLOC_CAP_SPIRAM)`), `st.home = geo::makeHome(HOME_LAT, HOME_LON)`, create the radar view then the status bar, then two LVGL timers:
  - `input_timer` every `INPUT_DRAIN_MS`: `while (knobInputPop(e))`: ROTATE_* → `st.zoom.handleEvent(e)`; PRESS / LONG_PRESS are no-ops in this task (Tasks 13 to 15 fill them in). After handling any rotate, `radarViewInvalidate`.
  - `tick_timer` every `UI_TICK_MS`: `st.zoom.tick(millis())`; if `st.zoom.animating()` invalidate the view; if `store->generation() != st.snap_generation`: `st.snap_n = store->snapshot(st.snap, MAX_AIRCRAFT); st.snap_generation = store->generation(); invalidate`. Once a second, `statusBarUpdate`.
  Touch drag: on the radar view add `LV_EVENT_PRESSING`: `lv_indev_get_vect(lv_indev_get_act(), &v)`; `st.pan_x_m += v.x / scalePxPerM(P); st.pan_y_m -= v.y / scalePxPerM(P);` then clamp so `hypot(pan_x, pan_y) <= 0.9 * view_radius_m`, invalidate. Double tap: `LV_EVENT_SHORT_CLICKED` twice within 350 ms → `pan = 0`, invalidate (keep a `last_click_ms` static). Note: a drag also fires `SHORT_CLICKED`? No; LVGL fires `SHORT_CLICKED` only when released without scrolling; set `lv_obj_clear_flag(rv, LV_OBJ_FLAG_SCROLLABLE)` so LVGL does not consume the drag as scroll, and treat a press as a drag once `|v| > 3 px`.

- [ ] **Step 4: Build, flash, verify.** Expected on screen: dark scope with 4 rings labelled 20/40/60/80 km, the 100 km ring at the edge, YSSY and YSBK squares, N tick, home dot, status text with an aircraft count once the first poll lands. Turning the knob clockwise zooms in smoothly, rings relabel (10 km spacing, then 5, then 2); anticlockwise back out; dragging pans; double tap recentres. Report the observed frame smoothness during zoom.

- [ ] **Step 5: Commit** `feat(ui): radar scope with rings, airports, status bar and knob zoom`.

---

### Task 12: Aircraft sprite layer

**Files:**
- Create: `flight_radar/src/aircraft_layer.h`, `flight_radar/src/aircraft_layer.cpp`, `tests/test_epoch.cpp` (host) and `flight_radar/src/epoch.h`
- Modify: `flight_radar/src/ui.cpp` (create the layer after the radar view; call `aircraftLayerTick` from `tick_timer`)

**Interfaces:**
- Consumes: `plane_24`, `UiState`, `spriteMotionUpdate`, `geo::project`, `radarViewProjection`, `netNowEpoch`.
- Produces:

```cpp
// epoch.h  (pure)
#pragma once
#include <stdint.h>
inline uint32_t epochFrom(uint32_t last_poll_epoch, uint32_t last_poll_millis, uint32_t now_millis) {
  return last_poll_epoch == 0 ? 0 : last_poll_epoch + (now_millis - last_poll_millis) / 1000;
}
// aircraft_layer.h
#pragma once
#include <lvgl.h>
#include "ui_state.h"
void aircraftLayerCreate(lv_obj_t *parent, UiState *st, lv_obj_t *radar_view);
void aircraftLayerTick(uint32_t now_ms);          // every UI_TICK_MS
int  aircraftLayerHitTest(lv_coord_t x, lv_coord_t y);   // index into st->snap or -1, within TAP_HIT_RADIUS_PX
```

- [ ] **Step 1: Host test for epoch.h** in `tests/test_epoch.cpp`: `epochFrom(0, 0, 5000) == 0`; `epochFrom(1790568225, 10000, 35000) == 1790568250`; millis wrap: `epochFrom(100, 4294967000u, 1000) == 100 + (uint32_t)(1000 - 4294967000u)/1000` which is 100 + 1. Make `netNowEpoch()` in `net_task.cpp` use `epochFrom` (edit it). Run `make -C tests`.

- [ ] **Step 2: aircraft_layer.cpp.** Pool: `lv_obj_t *img[MAX_AIRCRAFT]`, `lv_obj_t *label[MAX_AIRCRAFT]`, one `lv_obj_t *sel_ring` (an `lv_arc` 34 px, arc width 2, `#FFFFFF`, no knob, hidden). Create all at init: `lv_img_set_src(img, &plane_24)`, `lv_img_set_pivot(img, 12, 12)`, `lv_obj_set_style_img_recolor_opa(img, 255, 0)`, `lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN)`, `lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE)`. Labels: `lv_font_montserrat_10` (enable `LV_FONT_MONTSERRAT_10 1` in `lv_conf.h`; it is at line 365 area), colour `#F2B632`.
  `aircraftLayerTick(now_ms)`: `now_s = netNowEpoch()`; `P = radarViewProjection(rv)`; for `i < st->snap_n`: `spriteMotionUpdate(st->motion[i], st->snap[i].last, now_s, now_ms)`; project `cur_lat/lon`; if `!insideScope` or `!valid` hide img and label, continue; `lv_obj_set_pos(img, px - 12, py - 12)`; `lv_img_set_angle(img, (int16_t)(cur_track * 10) % 3600)`; recolour: selected `#FFFFFF`, else on_ground `#6F7A85`, else stale or frozen `#7A6A40`, else `#F2B632`; label shown when `viewRadiusM() <= LABEL_VIEW_RADIUS_M` or selected, positioned at `(px + 14, py - 6)`, text = callsign or icao24 when callsign empty. Hide pool entries `>= snap_n`. Selected: move `sel_ring` to `(px - 17, py - 17)`, show, and `lv_obj_move_foreground(img)`; hide the ring when nothing is selected. Because `snapshot()` rebuilds the array in store order and the store never reorders in-use slots, `motion[i]` stays aligned with the same aircraft between snapshots as long as slot `i` keeps the same icao24; on each new snapshot compare `snap[i].last.icao24` with a kept `char prev_icao[MAX_AIRCRAFT][7]` and zero `motion[i]` when it changed.
  `aircraftLayerHitTest`: nearest visible sprite centre within `TAP_HIT_RADIUS_PX`.

- [ ] **Step 3: ui.cpp**: after `radarViewCreate`, `aircraftLayerCreate(scr, &st, rv)`; in `tick_timer` after zoom tick and snapshot refresh call `aircraftLayerTick(millis())`. The snapshot must also be refreshed when the store generation changes before the tick (already the case).

- [ ] **Step 4: Build, flash, verify.** Expected: after the first poll, amber airplane sprites appear at plausible places (YSSY traffic lined up on the 16/34 runway axis), rotate with heading, and creep smoothly; ground aircraft grey. Zooming keeps them in place relative to rings. Report the sprite count and whether motion is smooth at 30 fps (LVGL's `LV_USE_PERF_MONITOR` can be enabled temporarily in `lv_conf.h` to read fps; turn it off before committing).

- [ ] **Step 5: Commit** `feat(ui): airplane sprites with dead reckoning and eased motion`.

---

### Task 13: Selection by knob press and touch, trails on screen

**Files:**
- Modify: `flight_radar/src/ui.cpp`, `flight_radar/src/radar_view.cpp` (trails already drawn in Task 11 read `st->selected_icao24`; verify), `flight_radar/src/aircraft_layer.cpp` (hit test)
- Create: `flight_radar/src/selection.h`, `flight_radar/src/selection.cpp` (pure), `tests/test_selection.cpp`

**Interfaces:**
- Produces:

```cpp
// selection.h (pure)
#pragma once
#include "aircraft.h"
#include "geo.h"
// Returns index of the next aircraft by distance from home after `current` (icao24 or empty), or -1 when wrapping past the last.
int selectNextByDistance(const Aircraft *snap, size_t n, const geo::Home &home, const char *current);
```

- [ ] **Step 1: Host test**: three aircraft at 5, 20, 50 km; `selectNextByDistance(snap, 3, H, "")` → index of the 5 km one; from the 5 km one → 20 km; from the 50 km one → -1; from an icao24 not in the snapshot → nearest; with `n == 0` → -1; aircraft with `in_use == false` skipped.
- [ ] **Step 2: Implement** by building an index array sorted by `geo::distanceM` (insertion sort is fine for 160).
- [ ] **Step 3: ui.cpp**: on PRESS: `int i = selectNextByDistance(st.snap, st.snap_n, st.home, st.selected_icao24)`; `i < 0 ? selected = ""` : copy icao24; invalidate the view. On `LV_EVENT_SHORT_CLICKED` on the radar view (not part of a drag, not a double tap): `lv_indev_get_point`, `i = aircraftLayerHitTest(x, y)`, select or clear. In `tick_timer`, if `selected_icao24` is set but no `snap[i]` matches, clear it (aircraft removed).
- [ ] **Step 4: Build, flash, verify**: press cycles nearest-first with a white ring and white callsign; the selected aircraft's trail is white and thicker; other trails amber and fading; tapping a sprite selects it; tapping empty scope clears. After two or three polls trails are visible behind moving aircraft and hug turns (the leader segment attaches to the sprite).
- [ ] **Step 5: Commit** `feat(ui): aircraft selection by knob press and touch`.

---

### Task 14: Detail card

**Files:**
- Create: `flight_radar/src/detail_card.h`, `flight_radar/src/detail_card.cpp`, `flight_radar/src/metadata_table.h` (stub), `flight_radar/src/metadata_table.cpp` (stub), `flight_radar/src/units.h`, `tests/test_units.cpp`
- Modify: `flight_radar/src/ui.cpp` (LONG_PRESS with selection toggles card; refresh card every second), `flight_radar/src/aircraft_store.cpp` (set `meta = metadataLookup(icao24)` on insert)

**Interfaces:**
- Produces:

```cpp
// metadata_table.h
#pragma once
#include <stdint.h>
struct MetadataRow { uint32_t icao24; char reg[8]; char typecode[5]; char model[24]; char op[24]; };
extern const MetadataRow METADATA[]; extern const size_t METADATA_COUNT;   // sorted by icao24
const MetadataRow *metadataLookup(const char *icao24_hex);                 // nullptr when absent
// units.h (pure, header-only)
#pragma once
#include <cmath>
#include <cstdio>
inline float mToFt(float m) { return m * 3.28084f; }
inline float mpsToKt(float v) { return v * 1.943844f; }
inline float mpsToFpm(float v) { return v * 196.8504f; }
// Writes "Unknown" when NaN, else formatted with the given printf format.
inline void fmtOrUnknown(char *out, size_t n, float v, const char *fmt) { if (std::isnan(v)) std::snprintf(out, n, "Unknown"); else std::snprintf(out, n, fmt, v); }
// detail_card.h
#pragma once
#include <lvgl.h>
#include "ui_state.h"
lv_obj_t *detailCardCreate(lv_obj_t *parent, UiState *st);
void      detailCardRefresh(lv_obj_t *card);   // rebuilds text from the selected snap entry; hides when none selected or card_open false
```

- [ ] **Step 1: Host test** `tests/test_units.cpp`: `mToFt(1000)` ≈ 3280.8; `mpsToKt(100)` ≈ 194.4; `mpsToFpm(-2.6)` ≈ -511.8; `fmtOrUnknown` with NaN gives "Unknown", with 2438.4 and `"%.0f ft"` gives "2438 ft". Also `metadataLookup("7c4e21") == nullptr` on the empty stub and `metadataLookup("zz") == nullptr`.
- [ ] **Step 2: metadata stub**: `const MetadataRow METADATA[] = {{0, "", "", "", ""}}; const size_t METADATA_COUNT = 0;` and a binary-search `metadataLookup` that parses the hex with `strtoul(..., 16)`. Add the files to the host build (they are pure).
- [ ] **Step 3: detail_card.cpp**: an `lv_obj` 300 x 250, aligned `LV_ALIGN_BOTTOM_MID` with y offset -8, radius 18, bg `#0C1218` opa 235, border 1 px `#2F8F5A`, hidden by default, not clickable. Children: header label (`lv_font_montserrat_20`, white), and a single multi-line body label (`lv_font_montserrat_12`, `#C8D2DC`) whose text is built with `snprintf` as `"ICAO24  %s\nSquawk  %s\nAlt     %s (%s)\nV/S     %s\nGS      %s\nTrack   %s\nLat/Lon %s, %s\nReg     %s\nType    %s\nModel   %s\nOperator %s\nGround  %s\nUpdated %us ago"` using `fmtOrUnknown` for the numeric fields (`"%.0f ft"`, `"%.0f m"`, `"%.0f fpm"`, `"%.0f kt"`, `"%.0f°"`, `"%.4f"`), metadata strings or "Unknown", and `ago = netNowEpoch() - last.last_contact`. Header = callsign or icao24.
- [ ] **Step 4: ui.cpp**: LONG_PRESS with a selection → `st.card_open = !st.card_open; detailCardRefresh(card)`. Every second in `tick_timer` call `detailCardRefresh`. Clearing the selection sets `card_open = false`.
- [ ] **Step 5: Build, flash, verify**: select an airborne aircraft, long press: card appears with plausible altitude in ft, speed in kt, squawk, "Unknown" for Reg/Type/Model/Operator, Updated counting up and resetting after each poll; long press again hides it; rotating still zooms with the card open.
- [ ] **Step 6: Commit** `feat(ui): aircraft detail card`.

---

### Task 15: Settings screen and persisted trail length

**Files:**
- Create: `flight_radar/src/settings_screen.h`, `flight_radar/src/settings_screen.cpp`
- Modify: `flight_radar/src/ui.cpp` (LONG_PRESS without selection opens it; routes rotate/press/long press to it while open; loads the saved value at init)

**Interfaces:**
- Produces:

```cpp
#pragma once
#include <lvgl.h>
#include "ui_state.h"
lv_obj_t *settingsScreenCreate(lv_obj_t *parent, UiState *st);
void      settingsScreenOpen(lv_obj_t *s);
bool      settingsScreenHandle(lv_obj_t *s, InputEvent e);   // true if consumed; PRESS commits st->trail_window_s and closes, LONG_PRESS cancels and closes
bool      settingsScreenIsOpen(lv_obj_t *s);
uint32_t  settingsLoadTrailWindow();        // Preferences "radar"/"trail_s", default TRAIL_WINDOW_DEFAULT_S
void      settingsSaveTrailWindow(uint32_t s);
```

- [ ] **Step 1: Implement.** A centred 260 x 200 panel like the card, title "TRAIL LENGTH", four rows `5 min / 10 min / 20 min / 30 min` as labels; the highlighted row gets bg `#2F8F5A` and black text. Rotate moves the highlight with clamping. PRESS: `st->trail_window_s = value; settingsSaveTrailWindow(value); close; st->settings_open = false`. Use `Preferences` from the core (`prefs.begin("radar", false); prefs.putUInt("trail_s", s)`).
- [ ] **Step 2: ui.cpp**: at init `st.trail_window_s = settingsLoadTrailWindow()`. In `input_timer`, if `settingsScreenIsOpen`, route every event to `settingsScreenHandle` first and skip the normal handling; on close invalidate the view. LONG_PRESS with no selection → `settingsScreenOpen`.
- [ ] **Step 3: Build, flash, verify**: long press with nothing selected opens the panel; rotate moves the highlight; press commits; trails visibly shorten with 5 min; power-cycle the board and confirm the value persisted (long press again shows the saved row highlighted).
- [ ] **Step 4: Commit** `feat(ui): trail length settings persisted in NVS`.

---

### Task 16: Offline aircraft metadata table (best effort)

**Files:**
- Create: `tools/gen_metadata_table.py`
- Modify (generated): `flight_radar/src/metadata_table.cpp`

- [ ] **Step 1: Find the CSV.** Try `curl -sI https://opensky-network.org/datasets/metadata/aircraft-database-complete-2025-08.csv` and the directory listing `https://opensky-network.org/datasets/metadata/`; the file is a few hundred MB. If no file with columns `icao24,registration,manufacturername,model,typecode,operator` is reachable within 10 minutes, stop, leave the stub, and report.
- [ ] **Step 2: Generator**: stream the CSV, keep rows where `0x7c0000 <= int(icao24,16) <= 0x7fffff`, truncate strings to the struct widths (7, 4, 23, 23 chars), escape quotes and backslashes, sort by icao24, write `metadata_table.cpp` in the same shape as the stub with `METADATA_COUNT = N`. Expect roughly 15 to 25 thousand rows, about 1.5 MB of flash; the app partition is 3 MB and the sketch was 0.7 MB, so check `tools/build.sh` output stays under 3 MB. If it does not fit, drop the `model` column width to 16 and the operator to 16.
- [ ] **Step 3: Host tests**: extend `tests/test_units.cpp` so `metadataLookup("7c6b2c")` (a Qantas frame) or any row present returns non-null with a non-empty `reg`, guarded by `if (METADATA_COUNT > 0)`.
- [ ] **Step 4: Build, flash, verify**: select an Australian-registered aircraft; the card shows Reg/Type/Model/Operator.
- [ ] **Step 5: Commit** `feat(metadata): offline registration and type table for Australian aircraft`.

---

### Task 17: README and final device check

**Files:**
- Modify: `README.md`

- [ ] **Step 1**: Document the knob mapping, the settings screen, what each status string means, poll rate and the OpenSky daily credit maths, how to regenerate airports, sprite and metadata, and the tests. Note that the metadata endpoint returned 410 on 2026-09-28, which is why the offline table exists.
- [ ] **Step 2**: Run `make -C tests` and `tools/build.sh` one final time; flash; watch `tools/monitor.sh "" 60` for a clean boot, `[net] http=200`, and free heap stable across three polls (no leak: the `heap=` value in the log should not fall poll over poll).
- [ ] **Step 3: Commit** `docs: usage, knob mapping and maintenance notes`.
