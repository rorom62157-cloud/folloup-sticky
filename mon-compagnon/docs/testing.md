# Verification

## Host domain

Run `./scripts/test-core.sh` (clang++ C++17, Wall/Wextra/Werror/pedantic). Covers valid/invalid sleep delays, same inactivity clock, blockers, deep physical wake, monotonic time, four-minute capture, hold/long release, save failure/retry, category-specific actions, confirmed deletion, pending reopen, session pending/approval/expiry/revoke, unauthorized access and mutation renewal. No actual audio, SD, display or network is exercised.

Independent reviewer reproduced two missing transitions. Author added regressions and corrected them. Integration reruns the suite after corrections.

## Web

Syntax checked using Node `vm.Script` through the available runtime. HTML/assets and absence of unsafe HTML insertion checked. Independent security/correctness review before publishing. Browser integration testing is still pending; no real HTTP backend present. Do not treat a static page as a validated authorization boundary.

## Diagnostic build

Installed Arduino CLI 1.5.1, Arduino Espressif 3.3.8, ESP32-S3 16MB flash, octal PSRAM, USB CDC. `./scripts/build-diagnostic.sh` compiled successfully: 340346 program bytes and 24012 static RAM bytes. This is the diagnostic only, not the application.

Generated binaries under artifacts/diagnostic and build output are ignored by Git. No unverified executable was flashed.

## Hardware access

/dev/cu.usbmodem14501 detected; read-only serial open succeeded. esptool chip-id failed opening the port with Operation not permitted; chip identity not retrieved. No erase/flash succeeded. Do not infer that all sensors or flash size have been tested.

Next physical test after access is available: preserve/recover existing firmware, verify device identity, flash a verified image through an authorized tool, then inspect serial diagnostic and press the physical inputs. Hardware power button and sleep require a separate functional firmware test. Do not ask the user for current measurements.

## Streaming WAV

`./scripts/test-storage.sh` compiles independent host tests. Coverage includes four-minute exact cap, all-or-nothing limit rejection, header lengths, exclusive .part creation, existing final preservation, abort recovery, injected short writes/ENOSPC, fsync failure, FAT-style no-replace rename and rename collisions. A host build with ESP_PLATFORM exercises stat branch only; it is not an SDK test.

`./scripts/build-core-smoke.sh` stages exact core sources and cross-compiles/links with installed ESP32-S3 SDK. The image is a compile-only check, NEVER a user firmware or diagnostic to flash.

ESP32-S3 core smoke compilation/link succeeded: 317502 program bytes, 22548 static RAM bytes. This validates SDK compilation/link compatibility of the staged domain/WAV sources, not microphone or FAT execution on hardware. Host core and WAV suites passed again after corrections.

## Web async regressions

Run `node tests/web_races.mjs` in an environment with Node. Executed through the available Node runtime and passed: newest refresh wins out of order; starting a mutation invalidates an older snapshot; session invalidation while a JSON body is parsing rejects the old result. This executes the actual client functions with a minimal transport fixture. It does not validate layout, DOM event behavior or the future HTTP authorization backend.
