# Verification

## Reprise Work — dernier lot

Les contrôles hardware historiques ci-dessous sont complétés par `hardware-test-status.md` : les deux nouvelles ouvertures série sont refusées, aucune commande `p` envoyée et aucune nouvelle recette matérielle.

- `python3 tests/diagnostic_preflight_tests.py` : quatre régressions réussies, parseurs et lanceur réel avec faux outil hors matériel ; contrôle du défaut sans erase et arrêt avant write si sécurité activée.
- `sh scripts/test-note-files.sh` : fichiers réels et NoteStore, tests stricts et sanitizer réussis ; revue indépendante sans défaut confirmé.
- `sh scripts/build-storage-probe.sh` : compilation/lien ESP32-S3 réussis, 325418 octets programme / 22628 statiques ; jamais flasher ce probe.
- Suites core, WAV, notes et HTTP : toutes réussies à nouveau. Le web n'est pas modifié dans ce lot.
- `sh scripts/build-diagnostic.sh` : compilation neuve réussie, 340426 octets programme / 24012 statiques ; table de partitions générée validée par `check-diagnostic-preflight.py partitions`.

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

## Notes and HTTP integration slice

`sh scripts/test-notes.sh` exercises restart, revisions, UTF-8/size bounds and injected I/O failures. Separate ASan/UBSan run passed. Corruption now fails closed and preserves files; the stale-ID regression denies replacement creation and stale deletes after damaged recovery. See `note-storage.md`.

`sh scripts/test-http.sh` exercises preapproval rejection, wrong/duplicate cookies, CSRF/Origin/Host/Fetch Metadata checks, encoded traversal, byte bounds, expiry without polling renewal, revoke and CSRF rotation. It tests actual portable policy, not a running socket server.

`sh scripts/build-http-probe.sh` stages exact assets and cross-compiles ESP-IDF HTTP transport under Arduino ESP32 3.3.8. The harness intentionally does not start a network/server; never flash it. No browser-to-device or physical prompt acceptance is claimed. Independent review confirmed the two storage/transport fixes on current sources and both host suites passed.

Updated hardware preparation and recovery results: `hardware-test-status.md`. Diagnostic compile and offline `image-info` passed, serial access denied. No flash/erase or on-device tests succeeded.

Second-slice cross-compilation results: complete core smoke 319014 program / 22628 global RAM bytes; final HTTP transport 463654 program / 25068 global RAM bytes. Both commands exited 0.

Expanded `tests/web_races.mjs` executed through Node runtime after final edits: all assertions passed, including bootstrap GET/POST ordering, retry cleanup, JSON UTF-8/escaping byte limits and exact note title/text boundaries. Fixture uses minimal DOM/transport; it is not a browser/device test.
