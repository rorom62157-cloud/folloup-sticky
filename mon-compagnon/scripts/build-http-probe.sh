#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cli=${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}
stage="$root/build/companion_probe"
mkdir -p "$stage"
cp "$root/firmware/companion_probe/"* "$stage/"
cp "$root/core/compagnon.hpp" "$root/core/compagnon.cpp" "$root/core/http_access.hpp" "$root/core/http_access.cpp" "$stage/"
python3 "$root/scripts/embed-web.py" "$root/web" "$stage/web_assets.hpp"
"$cli" compile --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi' --build-path "$root/build/http-probe-objects" "$stage"
