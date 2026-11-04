#!/bin/sh
# Actual alpha application target. Does not upload or install dependencies.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cli=${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}
mkdir -p "$root/build" "$root/artifacts/device"
stage=$(mktemp -d "$root/build/device-stage.XXXXXX")
objects=$(mktemp -d "$root/build/device-objects.XXXXXX")
trap 'rm -rf -- "$stage" "$objects"' EXIT HUP INT TERM
mkdir "$stage/device"
cp "$root"/firmware/device/*.cpp "$root"/firmware/device/*.hpp "$root"/firmware/device/device.ino "$stage/device/"
cp "$root"/core/*.hpp "$root"/core/*.cpp "$stage/device/"
cp "$root"/firmware/storage/posix_note_files.* "$stage/device/"
cp "$root"/firmware/companion_probe/embedded_http.hpp "$stage/device/"
python3 "$root/scripts/embed-web.py" "$root/web" "$stage/device/web_assets.hpp"
"$cli" compile --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=default' --build-path "$objects" --output-dir "$root/artifacts/device" "$stage/device"
