#!/bin/sh
# Compile/link the portable modules against the installed ESP32 runtime. NEVER flash this image.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cli=${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}
if [ ! -x "$cli" ]; then cli=arduino-cli; fi
stage="$root/build/core_smoke"
mkdir -p "$stage"
cp "$root"/core/*.cpp "$root"/core/*.hpp "$stage/"
cat > "$stage/core_smoke.ino" <<'SKETCH'
#include <Arduino.h>
#include "compagnon.hpp"
#include "wav_writer.hpp"
// Keep file-system entry points in the link without executing them.
volatile bool exercise_storage = false;
void setup() {
  compagnon::SleepPolicy policy;
  policy.evaluate(30000, false);
  compagnon::WavWriter writer;
  if (exercise_storage) {
    const unsigned char samples[2] = {0,0};
    writer.begin("/sd/compile-smoke.wav");
    writer.append(samples, sizeof samples);
    writer.finish();
  }
}
void loop() { delay(1000); }
SKETCH
"$cli" compile --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi' --build-path "$root/build/core-smoke-objects" "$stage"
