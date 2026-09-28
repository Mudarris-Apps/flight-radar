#!/usr/bin/env bash
# tools/monitor.sh — usage: tools/monitor.sh [port] [seconds]
set -euo pipefail
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
PORT="${1:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"
SECS="${2:-20}"
timeout "$SECS" "$CLI" monitor -p "$PORT" -c baudrate=115200 || true
