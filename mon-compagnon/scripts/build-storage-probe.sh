#!/bin/sh
# Compile/link only. NEVER FLASH this storage harness.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cli=${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}
if [ ! -x "$cli" ]; then cli=arduino-cli; fi
stage="$root/build/storage_probe"
mkdir -p "$stage"
cp "$root/firmware/storage_probe/storage_probe.ino" "$stage/"
cp "$root/firmware/storage/posix_note_files.hpp" "$root/firmware/storage/posix_note_files.cpp" "$stage/"
cp "$root/core/note_store.hpp" "$root/core/note_store.cpp" "$stage/"
"$cli" compile --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi' --build-path "$root/build/storage-probe-objects" "$stage"
