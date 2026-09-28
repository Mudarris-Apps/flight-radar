#!/usr/bin/env bash
# tools/flash.sh  - usage: tools/flash.sh [port]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# arduino-cli: $ARDUINO_CLI if set, else the one on PATH, else the copy bundled
# with Arduino IDE 2 on macOS.
CLI="${ARDUINO_CLI:-$(command -v arduino-cli || echo "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli")}"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc"
# First USB serial port: /dev/cu.usbmodem* on macOS, /dev/ttyACM* on Linux.
PORT="${1:-$( { ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null || true; } | head -1)}"
[ -n "$PORT" ] || { echo "no /dev/cu.usbmodem* or /dev/ttyACM* port found; pass the port as an argument" >&2; exit 1; }
"$CLI" upload --fqbn "$FQBN" --input-dir "$ROOT/build" -p "$PORT" "$ROOT/flight_radar"
