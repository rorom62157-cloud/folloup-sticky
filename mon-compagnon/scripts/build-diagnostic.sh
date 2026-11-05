#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cli=${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}
if [ ! -x "$cli" ]; then cli=arduino-cli; fi
# Existing toolchain; no library installation, no network-required update, no flash.
# Arduino preserves partitions.csv in a reused build path. Use a fresh directory.
mkdir -p "$root/build"
diagnostic_build=$(mktemp -d "$root/build/diagnostic-clean.XXXXXX")
trap 'rm -rf -- "$diagnostic_build"' EXIT HUP INT TERM
"$cli" compile --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=default' --build-path "$diagnostic_build" --output-dir "$root/artifacts/diagnostic" "$root/firmware/diagnostic"
