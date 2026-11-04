# Status — 2026-11-04 — first foundation milestone

## Authorization and source
User approved development. Remote base: folloup-waveshare, SHA 1682b7e7d7ff9c6b23ab7e903c44b241f095b402. Delivery branch work/mon-compagnon-v1, new directory mon-compagnon/. Upstream firmware and main unchanged. Local history is independent; publish through GitHub API on remote ancestry, never force-push local root.

## Delivered in this milestone
- Portable C++17 sleep, recording/classification and physical session authorization policies; strict host tests pass.
- Streaming WAV writer with four-minute cap, FAT-compatible publish and fault-injection tests.
- French vanilla companion web client; contract and no-backend limitations explicit.
- Hardware pin reference, source branch and PWR/BOOT constraints.
- Arduino diagnostic and ESP32-S3 core/WAV smoke build compiled and linked successfully, not flashed.
- Specification, contracts, architecture, decisions, checks and agent worklog for IDE continuation.

## Review
Core independent review found pending-reopen and screen-change signaling defects; fixed with regressions and passing integration tests. Web review found an out-of-order refresh race; latest-request and session-generation guards implemented. Three Node transport regression scenarios pass. Full browser/device validation still pending.

## Environment and blockers
macOS with clang++, Python3.9 and Node through runtime. Arduino CLI1.5.1 / Espressif3.3.8 installed. ESP-IDF standalone tools not found. Shell outbound GitHub DNS unavailable; gh auth invalid; GitHub connector works.
USB /dev/cu.usbmodem14501 detected and read-only access succeeds. esptool opening the port is denied (Operation not permitted). No flash/erase succeeded and no firmware-on-device acceptance claimed.

## Google route
Manual OAuth Playground with user-owned Web client, Offline refresh token is the proposed developer provisioning route without personal backend. See ADR002. Previous statement that a native companion was mandatory was corrected. No credentials collected and no real Google calls tested.

## Next inputs and work
1. Integrate hardware adapters and application UI using verified Waveshare source, with license attribution for any reuse. Resolve reproducible production framework/toolchain (ESP-IDF preferred; Arduino available diagnostic only).
2. Integrate the WAV writer with microphone/SD drivers; implement persistent note/index/outbox and partial recovery; couple domain decisions to real operations.
3. Implement embedded HTTP backend enforcing physical session auth/CSRF on every private route, then browser integration tests.
4. Add Gemini and Google transports using real provisioned credentials only when needed; never fake ready/sync state.
5. Validate physical buttons/display/audio/SD and both sleeps with verified firmware once flash access works. Preserve recovery path.
6. PDF/EPUB, weather/capteurs and orientation remain planned. Tiny LLM deferred.

The assistant application is not complete or runnable on the device yet. Continue existing authorization; do not request a new go for routine next implementation. Ask only for concrete missing credentials or hardware access when that step is ready.
