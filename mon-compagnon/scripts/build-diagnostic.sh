#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cli=${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}
if [ ! -x "$cli" ]; then cli=arduino-cli; fi
# Existing toolchain; no library installation, no network-required update, no flash.
"$cli" compile --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi' --build-path "$root/build/diagnostic" --output-dir "$root/artifacts/diagnostic" "$root/firmware/diagnostic"
