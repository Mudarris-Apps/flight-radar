#!/usr/bin/env bash
# tools/flash.sh  — usage: tools/flash.sh [port]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc"
PORT="${1:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "no /dev/cu.usbmodem* port found" >&2; exit 1; }
"$CLI" upload --fqbn "$FQBN" --input-dir "$ROOT/build" -p "$PORT" "$ROOT/flight_radar"
