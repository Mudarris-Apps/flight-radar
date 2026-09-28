# flight-radar

A flight radar for a Viewe ESP32-S3 round knob display. It polls OpenSky for
aircraft near a home location and renders them on the round LCD, with the knob
used for zoom/selection.

## Board

Viewe ESP32-S3 round knob display (ESP32-S3, PSRAM, round touch LCD). The
Arduino sketch uses the `ESP32_Display_Panel` board abstraction plus LVGL v8
(`flight_radar/lv_conf.h`, `flight_radar/esp_*_conf.h`,
`flight_radar/lvgl_v8_port.*` — copied from the panel's example and not
edited by this project).

FQBN used for build/upload:

```
esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc
```

## Secrets

Copy the example and fill in real values (this file is gitignored, never commit it):

```bash
cp flight_radar/secrets.example.h flight_radar/secrets.h
```

`flight_radar/secrets.h` defines `WIFI_SSID`, `WIFI_PASS`,
`OPENSKY_CLIENT_ID`, `OPENSKY_CLIENT_SECRET`, `HOME_LAT`, `HOME_LON`.

## Host tests

Pure logic (geometry, parsing, state, etc.) is unit tested on the host, outside
Arduino, with a tiny header-only harness (`tests/test.h`) and a Makefile that
builds each `tests/test_*.cpp` as its own binary and runs it.

```bash
make -C tests
```

Expect output ending in `N tests, 0 failures` for each binary.

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
(Ctrl-C) and have a human hold the knob down while replugging the USB cable,
then retry. Don't keep retrying blindly — this means the board didn't drop
into its bootloader and needs the manual button-hold recovery.

## Monitor

```bash
tools/monitor.sh [port] [seconds]
```

Opens the serial monitor at 115200 baud for the given number of seconds
(default 20), then exits automatically. Uses `timeout` (coreutils, installed
via Homebrew at `/opt/homebrew/bin/timeout` on this machine) to bound how
long it stays open, since `arduino-cli monitor` otherwise runs forever.

Because the board's USB-CDC port re-enumerates on reset, there is a race
between the reset finishing and the monitor attaching — the very first boot
line is sometimes missed. If nothing appears, just reflash and re-run
`tools/monitor.sh` again.

## Project layout

- `flight_radar/flight_radar.ino` — sketch entry point (`setup()`/`loop()`).
- `flight_radar/secrets.h` — real credentials, gitignored, not committed.
- `flight_radar/secrets.example.h` — template for `secrets.h`.
- `flight_radar/src/config.h` — tunable constants shared across the app.
- `flight_radar/lv_conf.h`, `flight_radar/esp_*_conf.h`,
  `flight_radar/lvgl_v8_port.*` — vendored board/LVGL port files, not edited.
- `tools/build.sh`, `tools/flash.sh`, `tools/monitor.sh` — build/flash/serial
  helper scripts wrapping `arduino-cli`.
- `tests/` — host-side unit tests for pure logic (`test.h` harness,
  `Makefile`, `test_*.cpp` files); `tests/build/` is gitignored.
