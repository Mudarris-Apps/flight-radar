#!/usr/bin/env bash
# tools/monitor.sh - usage: tools/monitor.sh [port] [seconds]
set -euo pipefail
# arduino-cli: $ARDUINO_CLI if set, else the one on PATH, else the copy bundled
# with Arduino IDE 2 on macOS.
CLI="${ARDUINO_CLI:-$(command -v arduino-cli || echo "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli")}"
# First USB serial port: /dev/cu.usbmodem* on macOS, /dev/ttyACM* on Linux.
PORT="${1:-$( { ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null || true; } | head -1)}"
SECS="${2:-20}"
[ -n "$PORT" ] || { echo "no /dev/cu.usbmodem* or /dev/ttyACM* port found; pass the port as an argument" >&2; exit 1; }
# Stop after $SECS seconds with timeout (Linux) or gtimeout (macOS coreutils);
# without either, run until Ctrl-C.
if command -v timeout >/dev/null 2>&1; then
  LIMIT=(timeout "$SECS")
elif command -v gtimeout >/dev/null 2>&1; then
  LIMIT=(gtimeout "$SECS")
else
  echo "monitor.sh: neither timeout nor gtimeout found; running without a time limit (Ctrl-C to stop)" >&2
  LIMIT=()
fi
${LIMIT[@]+"${LIMIT[@]}"} "$CLI" monitor -p "$PORT" -c baudrate=115200 || true
