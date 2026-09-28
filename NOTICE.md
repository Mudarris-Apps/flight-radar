# Notices

The code in this repository is released under the MIT licence (see
`LICENSE`), with the exceptions below. Third-party files keep their
original licences and their SPDX headers are preserved.

## Files copied from other projects

These files come from the `lvgl_v8_port` example of ESP32_Display_Panel
(https://github.com/esp-arduino-libs/ESP32_Display_Panel), copyright
Espressif Systems (Shanghai) CO LTD:

| File | Licence |
| --- | --- |
| `flight_radar/lvgl_v8_port.cpp` | CC0-1.0 |
| `flight_radar/lvgl_v8_port.h` | CC0-1.0 |
| `flight_radar/esp_panel_board_supported_conf.h` | Apache-2.0 |
| `flight_radar/esp_panel_board_custom_conf.h` | Apache-2.0 |
| `flight_radar/esp_panel_drivers_conf.h` | Apache-2.0 |
| `flight_radar/esp_utils_conf.h` | Apache-2.0 |

The CC0-1.0 header on the two port files matches the upstream example
in ESP32_Display_Panel 1.0.4 (`examples/*/lvgl_v8_port/`).

`flight_radar/lv_conf.h` is LVGL's `lv_conf_template.h` for v8.4.0, as
shipped in the ESP32_Display_Panel examples, with a few settings changed. LVGL is MIT licensed, copyright LVGL Kft
(https://github.com/lvgl/lvgl).

## Libraries used at build time

These are not included in the repository; you install them yourself.
Licences were read from each library's `library.properties` and its
licence file.

| Library | Version | Licence | Source |
| --- | --- | --- | --- |
| LVGL | 8.4.0 | MIT | https://github.com/lvgl/lvgl |
| ESP32_Display_Panel | 1.0.4 | Apache-2.0 | https://github.com/esp-arduino-libs/ESP32_Display_Panel |
| ESP32_IO_Expander | 1.1.1 | Apache-2.0 | https://github.com/esp-arduino-libs/ESP32_IO_Expander |
| esp-lib-utils | 0.3.0 | Apache-2.0 | https://github.com/esp-arduino-libs/esp-lib-utils |
| ESP32_Knob | 0.0.1 | Apache-2.0 | https://github.com/esp-arduino-libs/ESP32_Knob |
| ESP32_Button | 0.0.1 | Apache-2.0 | https://github.com/esp-arduino-libs/ESP32_Button |
| ArduinoJson | 7.4.3 | MIT | https://github.com/bblanchon/ArduinoJson |
| Arduino core for ESP32 (arduino-esp32) | 3.3.11 | LGPL-2.1-or-later | https://github.com/espressif/arduino-esp32 |

## Data

- Airports: `flight_radar/src/airports_data.cpp` is generated from
  OurAirports' `airports.csv` (https://ourairports.com/data/). OurAirports
  releases its data to the public domain.
- Aircraft metadata: `flight_radar/src/metadata_table.cpp` is generated
  from the OpenSky Network aircraft database. The test fixture
  `tests/fixtures/opensky_states_sydney.json` is a response captured from
  the OpenSky Network REST API. Both are subject to the OpenSky Network
  General Terms of Use and Data License Agreement
  (https://opensky-network.org/about/terms-of-use), which license the
  data for non-profit research and non-profit education only.
- OpenSky asks that publications using its data cite:
  Matthias Schäfer, Martin Strohmeier, Vincent Lenders, Ivan Martinovic
  and Matthias Wilhelm, "Bringing up OpenSky: A large-scale ADS-B sensor
  network for research", ACM/IEEE International Conference on Information
  Processing in Sensor Networks, April 2014; and, for the website, The
  OpenSky Network, http://www.opensky-network.org.
- TLS root: `flight_radar/src/isrg_root_x1.h` holds the public ISRG Root
  X1 certificate from https://letsencrypt.org/certs/isrgrootx1.pem.

This project is not affiliated with or endorsed by the OpenSky Network,
OurAirports, Espressif, LVGL or Viewe.
