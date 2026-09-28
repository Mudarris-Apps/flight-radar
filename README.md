# flight-radar

A flight radar for a Viewe ESP32-S3 round knob display. It polls OpenSky for
aircraft near a fixed home location and renders them on the round LCD, with
the knob and the touchscreen used for zoom, selection and panning.

## Board

Viewe ESP32-S3 round knob display (ESP32-S3, PSRAM, round touch AMOLED, a
rotary encoder knob with a built-in push button). The Arduino sketch uses the
`ESP32_Display_Panel` board abstraction plus LVGL v8 (`flight_radar/lv_conf.h`,
`flight_radar/esp_*_conf.h`, `flight_radar/lvgl_v8_port.*` - copied from the
panel's example and not edited by this project).

The panel driver reports 472 x 466 pixels; the visible circle is 466 x 466.
The code centres on `min(hor_res, ver_res)` rather than assuming a fixed size.

FQBN used for build/upload:

```
esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc
```

## Secrets

Copy the example and fill in real values (this file is gitignored, never
commit it):

```bash
cp flight_radar/secrets.example.h flight_radar/secrets.h
```

`flight_radar/secrets.h` defines `WIFI_SSID`, `WIFI_PASS`,
`OPENSKY_CLIENT_ID`, `OPENSKY_CLIENT_SECRET`, `HOME_LAT`, `HOME_LON`. The
OpenSky client ID and secret come from an OpenSky Network account (API client
credentials, not the account password); `HOME_LAT`/`HOME_LON` are the centre
of the 100 km observation radius.

## Host tests

Pure logic (geometry, parsing, storage, dead reckoning, zoom, selection,
epoch math, airports, units) is unit tested on the host, outside Arduino,
with a tiny header-only harness (`tests/test.h`) and a Makefile that builds
each `tests/test_*.cpp` as its own binary and runs it with `clang++
-std=c++17`.

```bash
make -C tests
```

Expect output ending in `N tests, 0 failures` for each binary. Pure modules
never include `Arduino.h` or `lvgl.h`, so they build and run without a board
attached.

## Build (device)

```bash
tools/build.sh
```

Wraps `arduino-cli compile` with the project's FQBN, writing artifacts to
`build/` (gitignored). The first build compiles the whole ESP32 core and
LVGL and takes about a minute; later builds are incremental and much faster.
On success it prints sketch/RAM usage and exits 0.

## Flash

```bash
tools/flash.sh [port]
```

Wraps `arduino-cli upload`, using the port passed as an argument or the first
`/dev/cu.usbmodem*` device found. Requires `tools/build.sh` to have run first
(it uploads from `build/`).

**If it hangs at `Connecting....` for more than ~40 seconds:** stop it
(Ctrl-C) and have a human hold the knob's button down while replugging the
USB cable, then retry. Don't keep retrying blindly - this means the board
didn't drop into its bootloader and needs the manual button-hold recovery.
(The button is on GPIO0, the same pin as the BOOT strap, which is why
holding it while replugging forces the ROM bootloader.)

## Monitor

```bash
tools/monitor.sh [port] [seconds]
```

Opens the serial monitor at 115200 baud for the given number of seconds
(default 20), then exits automatically.

`tools/monitor.sh` has been unreliable on this machine: `arduino-cli monitor`
sometimes attaches after the board's boot lines have already gone by, or
exits early. When that happens, fall back to raw `stty`/`cat` in two steps:

```bash
stty -f /dev/cu.usbmodem1101 115200 raw -echo
cat /dev/cu.usbmodem1101
```

(Ctrl-C to stop `cat`; adjust the device path to whatever port the board
enumerates as.) Because the board's USB-CDC port re-enumerates on reset,
there is a race between the reset finishing and the monitor attaching - the
very first boot line is sometimes missed either way. If nothing appears,
reflash and reattach.

## Using it

**Knob (rotate):** zooms the view in and out. The encoder's raw direction is
inverted in software so that turning it clockwise always zooms in, regardless
of which way the hardware wires PHA/PHB - anticlockwise zooms out. Each
detent is a fixed step; the range runs from an 8 km to a 105 km view radius.

**Knob (short press):** selects the next aircraft ordered by distance from
home. Pressing again moves to the next one further out; pressing past the
last aircraft clears the selection. Button events in the first second after
boot are ignored (the BOOT strap pin reads as held during reset and would
otherwise fire a phantom press).

**Knob (long press, about 1.5 s):**
- With an aircraft selected: toggles the detail card for that aircraft.
- With nothing selected: opens the trail-length settings screen. While it is
  open, rotate moves the highlight between 5, 10, 20 and 30 minutes; a short
  press saves the highlighted value to flash (NVS) and closes the screen; a
  long press cancels without saving. While the settings screen is open, touch
  is ignored (it is knob-only).

**Touch:** tapping a plane selects it; tapping empty scope clears the
selection. Dragging pans the view (trails are hidden while dragging so the
frame stays cheap to redraw). Double-tapping recentres the pan to zero.
Tapping an open detail card passes through to the scope underneath it and can
select or deselect a plane - there is no touch target on the card itself
that intercepts the tap.

## What the status bar says

One line near the top of the scope, updated once a second from the network
task's state:

| Text | Meaning |
| --- | --- |
| `N aircraft · Ns ago` | Last poll succeeded; `N aircraft` is the current store count, `Ns ago` is time since that poll's reported timestamp. This includes OpenSky's own data delay (their state vectors are typically 15-25 s old when returned), not just time since our last HTTP call. |
| `WIFI CONNECTING` | Wi-Fi association in progress or just failed, retrying. |
| `WIFI DOWN` | Three Wi-Fi connection attempts (10 s each) have failed in a row. |
| `OPENSKY AUTH FAILED` | The OAuth token request failed, or a 401 came back from `/states/all` and the token was discarded; retries every 30 s. |
| `OPENSKY HTTP 429 · retry N min` | Rate limited; backed off to a 300 s poll interval. |
| `OPENSKY HTTP <code>` / `OPENSKY ERROR` | Any other non-200 response, or a response whose body failed to parse. |

## Polling and the OpenSky credit budget

The network task polls `/states/all` every 25 s (`POLL_INTERVAL_S` in
`flight_radar/src/config.h`). OpenSky bills API credits by bounding-box area:
a box up to 100 km x 100 km costs 1 credit per call. The free tier's daily
allowance is 4000 credits, i.e. one call every 21.6 s to stay under budget
over 24 hours; polling every 25 s uses at most 3456 calls/day, leaving
headroom.

The task backs off to a 300 s interval (`POLL_BACKOFF_S`) whenever the server
returns HTTP 429, or whenever the `X-Rate-Limit-Remaining` header drops below
100 (`RATE_REMAINING_FLOOR`) - both logged as `[net] http=... rate=...`
lines. The OAuth token is refreshed 120 s before its stated expiry (observed
as 1800 s) rather than waiting for a 401. On any other error the task retries
after 30 s (`POLL_ERROR_RETRY_S`).

TLS to `opensky-network.org` and `auth.opensky-network.org` is pinned to the
ISRG Root X1 certificate (Let's Encrypt's root; both hosts chain to it).
`OPENSKY_TLS_INSECURE` in `config.h` is a diagnostic-only escape hatch that
switches to `setInsecure()` when set to `1` - never ship or leave it that
way; it exists only to isolate whether a connection failure is a TLS/cert
problem versus something else.

## Aircraft metadata: why it's an offline table

The detail card shows callsign, ICAO24, squawk, altitude, vertical speed,
ground speed, track, latitude/longitude, on-ground state and an "updated Ns
ago" figure, all from OpenSky's live `/states/all` response. Origin,
destination and airline logo are not available from that endpoint and are
not shown at all - as of 2026-09-28, OpenSky's live flight-metadata endpoint
returned HTTP 410 (gone) and its routes endpoint returned 404 during
development, so there is no live source for them.

Registration, type, model and operator come instead from a static table
baked into flash at build time (`flight_radar/src/metadata_table.cpp`,
generated), looked up by ICAO24 hex address. When an aircraft isn't in the
table the card shows "Unknown" for those four fields. The table is
restricted to the Australian ICAO24 address block (`0x7C0000`-`0x7FFFFF`)
and currently holds 11,252 rows with a registration (rows with no
registration are dropped, since the card has nothing useful to show for
them). Flying somewhere else will show mostly "Unknown" metadata unless the
table is regenerated for that region's address block.

## Regenerating generated files

Three files under `flight_radar/src/` are generated by scripts in `tools/`
and then committed - the board never generates them itself:

```bash
# Airports (large + medium, worldwide) from OurAirports' airports.csv.
# Downloads the CSV if no path is given.
tools/gen_airports.py [path-to-airports.csv]

# 24x24 top-down airplane silhouette sprite, pure Python (no Pillow needed),
# rasterised with 8x supersampling into an LVGL alpha-8 C array.
tools/gen_plane_sprite.py

# Offline registration/type/model/operator table, Australian block only.
# Pass a local CSV path or a URL to OpenSky's aircraft database export
# (https://s3.opensky-network.org/data-samples/metadata/aircraftDatabase.csv,
# reached in practice via a redirect from opensky-network.org's own dataset
# page - the mirror location has moved before and may move again).
tools/gen_metadata_table.py <csv-path-or-url>
```

Re-run any of these after a data source updates, or to point the metadata
table at a different address block. If `gen_metadata_table.py` can't reach
its source, the previously committed table stays as-is; the build does not
depend on network access.

## Known limitations

- Frame rate drops to about 7 fps during the 1 s ease that follows each poll
  when roughly 50 aircraft are on screen; it recovers once the ease
  finishes.
- Trails are hidden while zooming or dragging the view, to keep those
  interactions responsive; they reappear once the gesture settles.
- The detail card's bottom corners are visually clipped by the display's
  round glass bezel on this panel.
- A tap on an open detail card passes through to the scope beneath it (see
  "Using it" above) rather than being absorbed by the card.
- The "ago" figure in the status bar and detail card includes OpenSky's own
  reporting delay (typically 15-25 s) in addition to time since our last
  successful poll.

## Diagnostics

`RADAR_DIAG` in `flight_radar/src/config.h` (0 by default) enables extra
serial logging of per-frame UI timing: tick period, sprite counts, LVGL
refresh/redraw timings bucketed by what the UI was doing (idle, the
post-poll ease, zooming/dragging), stack high-water mark and free heap.
Turn it on when investigating frame-rate or memory issues; leave it off
otherwise, since it adds serial traffic every few seconds.

## Project layout

- `flight_radar/flight_radar.ino` - sketch entry point (`setup()`/`loop()`).
- `flight_radar/secrets.h` - real credentials, gitignored, not committed.
- `flight_radar/secrets.example.h` - template for `secrets.h`.
- `flight_radar/src/config.h` - tunable constants shared across the app
  (poll interval, zoom range, display rotation, diagnostics flag, etc).
- `flight_radar/lv_conf.h`, `flight_radar/esp_*_conf.h`,
  `flight_radar/lvgl_v8_port.*` - vendored board/LVGL port files, not edited.
  `lv_conf.h` sets `LV_COLOR_16_SWAP 1` (the panel wants byte-swapped RGB565)
  and a raised `LV_INV_BUF_SIZE` (128, so the sprite layer's many small
  invalidated rectangles don't overflow LVGL's per-frame list). The image is
  rotated 180 degrees in software via `DISPLAY_ROTATION_DEG` in `config.h`,
  since this panel (SH8601) has no mirror-Y/swap-XY hardware option and the
  flex cable on this build exits to the right.
- `flight_radar/src/*.h/.cpp` - the app: geometry, aircraft storage,
  OpenSky JSON parsing, dead reckoning, zoom controller, the OpenSky HTTP
  client, the network task, knob input, the radar scope widget, the sprite
  layer, the detail card, the settings screen, the status bar, and the `ui`
  glue module that owns UI state and dispatches input events.
- `flight_radar/src/airports_data.cpp`, `flight_radar/src/metadata_table.cpp`,
  `flight_radar/src/sprites/plane_24.c` - generated, committed (see
  "Regenerating generated files").
- `tools/gen_airports.py`, `tools/gen_plane_sprite.py`,
  `tools/gen_metadata_table.py` - the generators above.
- `tools/build.sh`, `tools/flash.sh`, `tools/monitor.sh` - build/flash/serial
  helper scripts wrapping `arduino-cli`.
- `tests/` - host-side unit tests for pure logic (`test.h` harness,
  `Makefile`, `test_*.cpp` files, `fixtures/` sample OpenSky responses);
  `tests/build/` is gitignored.
