# Flight radar for the Viewe ESP32-S3 knob display

Date: 2026-09-28. Status: approved in conversation, implementation started same day.

## Purpose

A dedicated aviation radar that runs on the Viewe UEDX46460015-MD50ET
(466 x 466 round AMOLED, ESP32-S3-R8, rotary knob with push button,
capacitive touch). It shows live aircraft within 100 km of a fixed
location as rotated airplane sprites on a dark radar scope, with trails,
knob-driven zoom, selection, and a detail card. Data comes from the
OpenSky Network REST API over the board's WiFi.

The default location in the example config is Sydney Airport (latitude
-33.9461, longitude 151.1772); set your own in `secrets.h`. Within the
100 km ring: YSSY, YSBK, YSCN, YSRI, YSHL and Western Sydney
International.

## Decisions already made

- Radar scope background, no map tiles.
- Fixed location from a gitignored config header. No GPS, no IP
  geolocation, no search.
- Knob mapping (final):
  - rotate: zoom in / out
  - short press: select next aircraft by distance from centre, wrapping;
    pressing past the last one clears the selection
  - long press with a selection: toggle the detail card
  - long press with no selection: open the settings screen (trail
    length); rotate changes the value, press confirms and closes
  - touch tap on a sprite selects it; tap on empty scope clears
  - touch drag pans; double tap recentres
- Architecture A: aircraft are `lv_img` objects moved by an
  interpolation timer; rings, ticks, airports, observation circle and
  trails are drawn by one custom widget in its draw callback.
- Arduino IDE project, built and flashed from the command line with
  arduino-cli. Pure logic modules compile on the host with clang++ and
  have unit tests.
- Origin, destination and airline logo are not available from OpenSky
  and are omitted from the detail card. Registration, type, model and
  operator are only available from an optional offline table (see
  "Aircraft metadata"); when absent the card shows "Unknown".

## Hardware and toolchain facts

| Item | Value |
| --- | --- |
| Panel | SH8601 AMOLED over QSPI, 466 x 466 visible; library declares 472 x 466. Use `lv_disp_get_hor_res/ver_res` at runtime and centre on the smaller dimension. |
| Touch | CST820 over I2C, SDA IO1, SCL IO3, no INT/RST |
| Encoder | PHA IO6, PHB IO5 |
| Button | IO0 (also the BOOT strap; holding it while plugging in enters the ROM bootloader) |
| USB | native USB, `/dev/cu.usbmodem*`, Hardware CDC and JTAG |
| Core | arduino-esp32 3.3.11 |
| Libraries | ESP32_Display_Panel 1.0.4, esp-lib-utils 0.3.0, ESP32_IO_Expander 1.1.1, lvgl 8.4.0, ESP32_Knob 0.0.1, ESP32_Button 0.0.1, ArduinoJson 7.4.3 |
| FQBN | `esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc` |
| arduino-cli | `/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli` (1.5.1) |
| Build time | about 65 s clean |

The LVGL port (`lvgl_v8_port.cpp/.h`) and the five `*_conf.h` files are
copied unchanged from the ESP32_Display_Panel "lvgl_v8_port" example,
with `BOARD_VIEWE_UEDX46460015_MD50ET` enabled. LVGL runs in its own
task on core 1. Every LVGL call from another task goes inside
`lvgl_port_lock(-1)` / `lvgl_port_unlock()`.

## Repository layout

```
flight-radar/
  .gitignore                     flight_radar/secrets.h, build/
  README.md                      how to configure, build, flash, run tests
  docs/superpowers/specs/, docs/superpowers/plans/
  flight_radar/                  the Arduino sketch (folder name == .ino name)
    flight_radar.ino             setup(): board, LVGL, input, net task; loop(): idle
    secrets.example.h            committed template
    secrets.h                    gitignored: WIFI_SSID, WIFI_PASS, OPENSKY_CLIENT_ID,
                                 OPENSKY_CLIENT_SECRET, HOME_LAT, HOME_LON
    lv_conf.h, esp_*_conf.h, lvgl_v8_port.cpp/.h   from the example
    src/                         compiled recursively by arduino-cli
      config.h                   tunables (poll interval, radius, zoom range, trail options)
      geo.h/.cpp                 pure: projection, distance, bearing, bounding box
      aircraft.h                 pure: Aircraft struct, TrailPoint, enums
      aircraft_store.h/.cpp      pure: table keyed by icao24, upsert, expire, trails, snapshot
      opensky_parser.h/.cpp      pure (ArduinoJson): states JSON -> vector<AircraftReport>
      dead_reckoning.h/.cpp      pure: displayed position from report + elapsed, easing
      zoom_controller.h/.cpp     pure: view radius, zoomIn/zoomOut, handleEvent, animation target
      airports.h                 generated: large + medium airports worldwide
      opensky_client.h/.cpp      Arduino: token, states GET, rate-limit header, backoff
      net_task.h/.cpp            Arduino: WiFi lifecycle, poll loop, writes to store
      knob_input.h/.cpp          Arduino: ESP32_Knob + ESP32_Button -> InputEvent queue
      radar_view.h/.cpp          LVGL: custom widget draw callback (scope, trails, airports)
      aircraft_layer.h/.cpp      LVGL: sprite pool, interpolation timer, selection ring, labels
      detail_card.h/.cpp         LVGL: detail overlay
      settings_screen.h/.cpp     LVGL: trail length picker
      status_bar.h/.cpp          LVGL: top-of-scope status text
      ui.h/.cpp                  glue: owns UiState, dispatches InputEvents, drains store
      sprites/plane_24.c         LVGL A8 image, 24 x 24 top-down airplane, nose up
      metadata_table.h           generated, optional: icao24 -> registration/type/operator
  tools/
    gen_airports.py              OurAirports CSV -> src/airports.h
    gen_plane_sprite.py          renders the sprite to an LVGL A8 C array
    gen_metadata_table.py        OpenSky aircraft database CSV -> src/metadata_table.h (AU only)
    build.sh, flash.sh, monitor.sh   wrap arduino-cli with the FQBN above
  tests/
    Makefile                     clang++ -std=c++17, builds and runs every test_*.cpp
    test_*.cpp                   one per pure module
    fixtures/opensky_states_sydney.json   real response captured 2026-09-28
```

Pure modules must not include `Arduino.h` or `lvgl.h`. They may include
`<ArduinoJson.h>` (it builds on the host; the Makefile adds
`~/Documents/Arduino/libraries/ArduinoJson/src` to the include path).

## Data model

```cpp
struct AircraftReport {            // one row from /states/all
  char icao24[7];                  // lowercase hex
  char callsign[9];                // trimmed, may be empty
  uint32_t time_position;          // 0 if null
  uint32_t last_contact;
  float lat, lon;                  // NaN if null
  float baro_alt_m, geo_alt_m;     // NaN if null
  bool on_ground;
  float velocity_mps, track_deg, vertical_rate_mps;   // NaN if null
  char squawk[5];                  // empty if null
  uint8_t category;                // extended=1 field 17, 0 if absent
};

struct TrailPoint { float lat, lon; uint32_t t; };

struct Aircraft {
  AircraftReport last;             // most recent report
  uint32_t first_seen, last_seen_poll;   // poll timestamps
  TrailPoint trail[TRAIL_CAPACITY];      // ring buffer of reported positions
  uint16_t trail_head, trail_len;
  bool stale;                      // missed one poll
  // metadata (from offline table, may be empty)
  const MetadataRow *meta;
};
```

`AircraftStore` holds a fixed array of `MAX_AIRCRAFT = 160` in PSRAM
(`heap_caps_malloc(MALLOC_CAP_SPIRAM)` on device, `malloc` on host),
guarded by a mutex on device. Operations:

- `applyPoll(reports, poll_time)`: upsert every report; append a trail
  point only when `time_position` is newer than the last recorded point
  and lat/lon are valid; mark aircraft absent from this poll `stale`;
  remove aircraft absent for `STALE_REMOVE_S = 120` seconds. Aircraft
  farther than `OBS_RADIUS_M` from home are dropped even if the
  bounding box returned them.
- `snapshot(out)`: copies the table under the lock for the UI task.
- `trailWindow(aircraft, now, window_s, out)`: the points newer than
  `now - window_s`, oldest first.

`TRAIL_CAPACITY = 96` covers 30 minutes at the slowest poll rate with
margin. Memory: about 1.2 KB per aircraft, 200 KB total in PSRAM.

## Geometry

Local tangent plane around home:

```
x_m = (lon - HOME_LON) * cos(HOME_LAT) * 111320
y_m = (lat - HOME_LAT) * 110574
```

Screen: `px = cx + (x_m + pan_x) * scale`, `py = cy - (y_m + pan_y) *
scale`, with `scale = R_px / view_radius_m` where `R_px` is the scope
radius in pixels (half the smaller display dimension minus a 4 px
margin) and `view_radius_m` is the distance from centre to the scope
edge. Bounding box for the API: home ± 100 km converted to degrees,
sent as `lamin, lamax, lomin, lomax` with 4 decimals, plus
`extended=1`.

Sprite rotation: LVGL image angle in 0.1 degree units, clockwise from
north, so `angle = (int)(track_deg * 10)` for a sprite drawn nose-up.

## Zoom controller

```cpp
enum class InputEvent { ROTATE_LEFT, ROTATE_RIGHT, PRESS, LONG_PRESS,
                        TAP, DOUBLE_TAP, DRAG };
class ZoomController {
  float viewRadiusM();          // current, animated
  float targetRadiusM();
  void zoomIn();  void zoomOut();
  void handleEvent(InputEvent);   // ROTATE_RIGHT = zoomIn, ROTATE_LEFT = zoomOut
  void tick(uint32_t now_ms);     // eases current toward target, 150 ms
  void reset();                   // back to ZOOM_MAX_RADIUS_M
};
```

Range: `ZOOM_MAX_RADIUS_M = 105000` (100 km ring fully visible) down to
`ZOOM_MIN_RADIUS_M = 8000`. Each detent multiplies or divides the target
by `ZOOM_STEP = 1.12`, clamped. The full range is about 23 detents.

Pan is an offset in metres held in `UiState`, applied in the projection.
Panning is clamped so the home point stays within 90 % of the view
radius. Zooming keeps the current pan. Double tap or reset zeroes pan.

## Dead reckoning and interpolation

The UI never draws reported positions directly. For each aircraft at
frame time `t`:

1. `dr = last position advanced along track by velocity * (t -
   time_position)`, capped at `DR_MAX_EXTRAPOLATION_S = 90` seconds
   (beyond that the sprite freezes and is drawn dimmed). Aircraft on the
   ground or with NaN velocity/track are not advanced.
2. When a new report arrives, the sprite eases from its current
   displayed position to the new `dr` over `EASE_MS = 1000` instead of
   jumping.
3. Heading eases the short way round over the same window.

Trails use recorded `TrailPoint`s only, never the eased position. The
segment from the last recorded point to the current displayed position
is drawn as a thin "leader" so the trail visibly attaches to the sprite.

## Rendering

The scope is one full-screen custom LVGL object (`radar_view`) whose
`LV_EVENT_DRAW_MAIN` handler draws, in order:

1. Background: solid `#05080C`.
2. Range rings: pick the ring spacing from {2, 5, 10, 20, 25, 50} km so
   that 3 to 5 rings fit inside the view radius; draw with
   `lv_draw_arc` at 1 px, `#1B3A2A`; label the distance at the top of
   each ring in a 12 px font, `#3E7A55`.
3. Observation ring at 100 km: 2 px, `#2F8F5A`, only drawn when inside
   the view (it is the outer edge at minimum zoom).
4. Compass ticks every 30 degrees at the scope edge, `N` label.
5. Airports within the view: 5 px hollow square `#5AA0FF`, ICAO ident
   label when `view_radius_m <= 60000`.
6. Trails for every aircraft: segments between consecutive
   `TrailPoint`s in the window, opacity ramping from 20 % (oldest) to
   100 % (newest), 2 px, colour `#E0A030` (selected: `#FFFFFF`, 3 px).
   Trails of aircraft outside the view are skipped. Points are
   projected once per draw.
7. Home marker: 6 px filled circle `#FFFFFF` with a 1 px `#2F8F5A` halo.

`radar_view` is invalidated when: a poll applies, zoom target or current
value changes, pan changes, selection changes, trail window changes.
It is not invalidated per interpolation frame.

`aircraft_layer` keeps a pool of `MAX_AIRCRAFT` `lv_img` objects
(created once, hidden when unused) parented to the scope, using
`plane_24` (A8, recoloured through `img_recolor` at 255 opa). Colours:
airborne `#F2B632`, on ground `#6F7A85`, stale or frozen `#7A6A40`,
selected `#FFFFFF` with a 34 px hollow ring drawn by a small `lv_arc`
behind it. Each sprite gets a callsign label (10 px font) shown when
`view_radius_m <= 40000` or the aircraft is selected. A 33 ms LVGL timer
runs `tick()`: advances the zoom easing, computes the displayed position
for each aircraft, sets `x, y, angle` and colour. LVGL invalidates only
the sprite's old and new rectangles.

Sprites are sorted so the selected aircraft is on top. Sprites whose
projected position falls outside the scope circle are hidden.

## Selection and detail card

`UiState.selected_icao24` (empty when none). Short press: order the
current snapshot by distance from home, pick the next after the current
selection (or the first), or clear after the last. Tap: nearest sprite
within 22 px of the touch point, else clear.

Detail card: an `lv_obj` panel 300 px wide anchored to the bottom of
the scope with rounded corners, `#0C1218` at 92 % opacity, 1 px
`#2F8F5A` border. Rows (label left, value right, 12 px font; header
callsign in 20 px):

- Callsign, ICAO24, Squawk
- Altitude (baro, in ft and m), Vertical speed (ft/min), Ground speed
  (kt), Track (deg)
- Latitude, Longitude (4 decimals)
- Registration, Type, Model, Operator (from metadata table, else
  "Unknown")
- On ground (yes/no), Updated (seconds ago, refreshed every second)

The card hides automatically when the selected aircraft is removed.
Long press toggles it; it starts hidden. While it is open, rotate still
zooms.

## Settings screen

A modal panel listing trail lengths 5, 10, 20, 30 minutes with the
current one highlighted. Rotate moves the highlight, press confirms and
closes, long press cancels. The value lives in `UiState.trail_window_s`
and is persisted in NVS (`Preferences`, namespace `radar`, key
`trail_s`). Default 600.

## Status bar

A single label at the top inside the scope, 12 px, `#3E7A55`:
`"58 aircraft · 12 s ago"` on success, `"WIFI CONNECTING"`, `"WIFI
DOWN"`, `"OPENSKY AUTH FAILED"`, `"OPENSKY HTTP 429 · retry 5 min"`,
`"OPENSKY ERROR"` otherwise. Updated once a second by the UI.

## Network task

Pinned to core 0, priority 1, 24 KB stack (TLS). Loop:

1. Ensure WiFi is connected (`WiFi.begin`, retry every 10 s, log RSSI).
2. Ensure a token: POST
   `https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token`
   with `grant_type=client_credentials&client_id=...&client_secret=...`
   (form-encoded). Store `access_token`, refresh when within 120 s of
   `expires_in` (1800 s observed).
3. GET `https://opensky-network.org/api/states/all?lamin=..&lamax=..&lomin=..&lomax=..&extended=1`
   with `Authorization: Bearer`. Read `X-Rate-Limit-Remaining` and log
   it. Response is 8 to 30 KB; parse with ArduinoJson into a document
   allocated in PSRAM, then `AircraftStore::applyPoll`.
4. Sleep `POLL_INTERVAL_S = 25` (4000 credits per day allow one 1-credit
   call every 21.6 s; 25 s leaves headroom). On HTTP 429 or when
   remaining < 100, back off to 300 s. On 401, drop the token and
   re-authenticate once. On any other error, retry after 30 s.

TLS: `WiFiClientSecure::setCACert(ISRG_ROOT_X1_PEM)` (the real chain is
Let's Encrypt). `config.h` has `OPENSKY_TLS_INSECURE 0`; setting it to 1
switches to `setInsecure()` as a diagnostic fallback only.

Only `net_task` touches WiFi and HTTP. It communicates through
`AircraftStore` and a small `NetStatus` struct (state enum, last
success time, aircraft count, rate remaining) protected by the same
mutex.

## Input

`knob_input` wraps `ESP_Knob(6, 5)` and `Button(GPIO_NUM_0, pullup=true,
...)` from the Espressif libraries. Their callbacks push `InputEvent`s
into a FreeRTOS queue of depth 32. The UI drains the queue from a 20 ms
LVGL timer under the LVGL lock. Direction is checked on hardware; if
clockwise produces LEFT events, call `invertDirection()`. Long press
threshold is the library default (about 1.5 s). Touch events come
through LVGL's own input device on the scope object (`LV_EVENT_CLICKED`,
`LV_EVENT_PRESSING` with a movement threshold for drag,
`LV_EVENT_SHORT_CLICKED` counting for double tap).

## Aircraft metadata (optional table)

`tools/gen_metadata_table.py` downloads OpenSky's aircraft database CSV
(URL argument; the current file lives under
`https://opensky-network.org/datasets/metadata/`), keeps rows whose
icao24 is in the Australian block `7c0000` to `7fffff`, and emits
`src/metadata_table.h` as a sorted `const MetadataRow[]` of
`{uint32_t icao24; char reg[8]; char typecode[5]; char model[24]; char
operator[24];}` in flash, with a binary-search lookup. If the tool is
never run, a committed stub with an empty table keeps the build working
and the card shows "Unknown". This is the last task in the plan and is
best effort: if the CSV is unavailable, the stub stays.

## Airports table

`tools/gen_airports.py` reads OurAirports `airports.csv`, keeps
`large_airport` and `medium_airport`, and writes `src/airports.h`:
`struct Airport { float lat, lon; char ident[5]; uint8_t large; }` sorted
by latitude for a cheap range scan. About 5300 rows, 70 KB in flash.
The generated file is committed.

## Sprite

`tools/gen_plane_sprite.py` draws a 24 x 24 anti-aliased top-down
airliner silhouette (fuselage, swept wings, tail) with Pillow, nose at
the top, and writes `src/sprites/plane_24.c` as an
`LV_IMG_CF_ALPHA_8BIT` image. The generated file is committed. If
Pillow is not installed the tool says so; the committed file is the
source of truth.

## Configuration

`secrets.example.h`:

```cpp
#define WIFI_SSID "..."
#define WIFI_PASS "..."
#define OPENSKY_CLIENT_ID "..."
#define OPENSKY_CLIENT_SECRET "..."
#define HOME_LAT -33.9461f
#define HOME_LON 151.1772f
```

`config.h` holds every tunable named in this spec with the given
defaults.

## Error handling

- No WiFi or API failure: the scope keeps showing the last snapshot,
  sprites keep dead-reckoning up to 90 s, then freeze dimmed; the
  status bar says why.
- JSON parse failure: log, count it, treat as a failed poll.
- Store full: new aircraft beyond `MAX_AIRCRAFT` are dropped with a log
  line.
- Touch or knob callback errors are impossible to surface on screen;
  they log to serial.
- Any `assert` on the board reboots it; `setup()` logs a boot reason.

## Testing

Host tests (`make -C tests`), one file per pure module:

- geo: projection round trip, distance to YSSY within 1 %, bounding box
  degrees.
- opensky_parser: the Sydney fixture parses to 58 reports; nulls become
  NaN or empty; callsign trimmed; on_ground preserved.
- aircraft_store: upsert then re-poll keeps identity; missing aircraft
  goes stale then removed after 120 s; trail appends only on newer
  `time_position`; window query returns oldest first; ring buffer wraps
  at capacity; out-of-radius aircraft dropped.
- dead_reckoning: 60 s at 250 m/s on track 90 moves about 15 km east;
  cap at 90 s; ground aircraft do not move; easing reaches target at
  1000 ms; heading eases the short way.
- zoom_controller: 23 detents span the range; clamps; easing converges;
  events map correctly.

On-device verification (manual, with the board attached):

- `tools/build.sh` compiles; `tools/flash.sh` uploads; `tools/monitor.sh`
  shows WiFi connected, token OK, rate remaining, aircraft count.
- Scope shows rings, YSSY/YSBK squares, sprites moving, trails growing
  after two polls, knob zooms, press cycles selection, long press opens
  the card, touch selects.

## Out of scope for this version

Map tiles, location search, IP geolocation, routes, airline logos,
OTA updates, captive-portal WiFi setup, multiple locations.
