#!/bin/bash
# Flash ESP-WordClock8x8.
# Usage: ./upload.sh [env] [port]
#   env  atoms3lite (default) | esp32 | esp8266
set -euo pipefail
cd "$(dirname "$0")"
ENV="${1:-atoms3lite}"
PORT="${2:-}"
if [[ -z "$PORT" ]]; then
  PORT=$(ls /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/cu.usbserial* 2>/dev/null | head -n1 || true)
fi
if [[ -z "$PORT" ]]; then
  echo "No USB serial port found."
  exit 1
fi
echo "Uploading env=$ENV  port=$PORT"
pio run -e "$ENV" -t upload --upload-port "$PORT"
