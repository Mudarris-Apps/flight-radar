# Contributing

Thanks for your interest. Bug reports, fixes and small improvements are
welcome. For anything larger, open an issue first so we can agree on the
approach before you spend time on it.

## Setting up

You need `arduino-cli` (the copy bundled with Arduino IDE 2 works) and
the Arduino core for ESP32 at version 3.3.11:

```bash
arduino-cli core install esp32:esp32@3.3.11 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Install these libraries into your Arduino libraries folder. These are the
versions the project is built and tested with:

| Library | Version |
| --- | --- |
| lvgl | 8.4.0 |
| ESP32_Display_Panel | 1.0.4 |
| ESP32_IO_Expander | 1.1.1 |
| esp-lib-utils | 0.3.0 |
| ESP32_Knob | 0.0.1 |
| ESP32_Button | 0.0.1 |
| ArduinoJson | 7.4.3 |

The scripts in `tools/` use `$ARDUINO_CLI` if you set it, otherwise
`arduino-cli` on your `PATH`, otherwise the copy inside Arduino IDE 2 on
macOS. `flash.sh` and `monitor.sh` pick the first `/dev/cu.usbmodem*`
(macOS) or `/dev/ttyACM*` (Linux) port unless you pass one.

Copy `flight_radar/secrets.example.h` to `flight_radar/secrets.h` and
fill in your WiFi details, OpenSky API client credentials and home
location, as the README describes.

## Running the host tests

The pure logic modules build and run on your computer without a board:

```bash
make -C tests
```

The Makefile uses `clang++` and looks for ArduinoJson in
`~/Documents/Arduino/libraries/ArduinoJson/src`. Override either one if
yours differ:

```bash
make -C tests CXX=clang++ ARDUINOJSON_DIR=/path/to/ArduinoJson/src
```

Every binary should end with `N tests, 0 failures`. Please add or update
tests when you change code under `flight_radar/src/` that the tests
cover.

## Building and flashing

```bash
tools/build.sh
tools/flash.sh [port]
tools/monitor.sh [port] [seconds]
```

The README covers what to do if flashing hangs at `Connecting....`.

## Secrets

- Never commit `flight_radar/secrets.h`. It is in `.gitignore`; keep it
  there.
- Never paste WiFi passwords, OpenSky client secrets or tokens into an
  issue, a pull request or a log excerpt. Redact them first. Take extra
  care with serial logs captured with the Arduino "Core Debug Level" at
  Debug or above: the ESP32 core then logs HTTP and TLS detail, and
  depending on the core version that can include the `Authorization`
  header with the OpenSky bearer token. Treat such logs as secret.
- If you think you have committed a secret, rotate it straight away, then
  tell us.

## The aircraft metadata table

`flight_radar/src/metadata_table.cpp` is committed as an empty stub. The
full table is derived from the OpenSky Network aircraft database under
OpenSky's terms of use, so it must never be committed or attached to an
issue or pull request. To use it on your own board, generate it locally:

```bash
python3 tools/gen_metadata_table.py <aircraftDatabase.csv path or URL>
```

Then tell git to ignore your local copy so it cannot slip into a commit:

```bash
git update-index --skip-worktree flight_radar/src/metadata_table.cpp
```

Undo that with `--no-skip-worktree` and `git checkout -- flight_radar/src/metadata_table.cpp`
if you need to change the stub itself. CI fails any push or pull request
where the committed file is not the stub. If you change the generator,
change the lookup code in the stub to match.

The parser test fixture, `tests/fixtures/states_sample.json`, is
synthetic. If you need different rows, change `tools/gen_test_fixture.py`
and rerun it rather than pasting a captured API response.

## Commit style

- Write the subject line in the imperative mood ("fix knob debounce", not
  "fixed knob debounce"), short enough to read in `git log --oneline`.
- Use the body to explain why the change is needed, not just what it
  does.
- Keep each commit to one logical change.

## Pull requests

Every push and pull request runs CI: a check that the metadata table is
still the stub, the host tests on Linux with `clang++`, and a gitleaks
secret scan over the full history. Please make
sure both pass.

CI does not build the firmware yet. A device build job (installing the
ESP32 core and the libraries above with `arduino-cli`) is planned; until
then, run `tools/build.sh` yourself before opening a pull request that
touches the sketch.
