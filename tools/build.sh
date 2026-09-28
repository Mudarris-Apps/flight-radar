#!/usr/bin/env bash
# tools/build.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# arduino-cli: $ARDUINO_CLI if set, else the one on PATH, else the copy bundled
# with Arduino IDE 2 on macOS.
CLI="${ARDUINO_CLI:-$(command -v arduino-cli || echo "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli")}"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc"
mkdir -p "$ROOT/build"
"$CLI" compile --fqbn "$FQBN" --build-path "$ROOT/build" --warnings default "$ROOT/flight_radar" "$@"
