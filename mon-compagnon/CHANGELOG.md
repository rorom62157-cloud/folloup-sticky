# Changelog

## 0.3.0 storage and hardware handoff — 2026-11-04

- Concrete FAT/POSIX note adapter, real filesystem tests and compile-only ESP32 storage probe.
- Interactive I²C diagnostic command `p`, compiled; installation still unconfirmed.
- Diagnostic launcher defaults to update-only, checks identity/security/partition table and uses a fresh build directory.
- User-provided boot/memory/button observations recorded separately from unvalidated peripherals.
- Cross-computer handover with source retrieval, toolchain, safe first serial test and remaining implementation work.
- Serial access remained denied; Computer Use failed to start and later became unavailable. No device flash by this session.

## 0.2.0 integration foundations — 2026-11-04

- Versioned notes/post-it store with injected filesystem and interruption/corruption tests.
- Cookie/CSRF/Origin HTTP boundary and ESP-IDF server transport compiling for ESP32-S3.
- Physical session mutex kept outside all network I/O; corruption blocks stale-ID reuse.
- Web bootstrap/UTF-8 bounds corrections; diagnostic USB mode and recovery procedure.
- Still not a runnable assistant firmware: SD/UI/audio/network adapters remain to integrate; hardware port access denied.

## 0.1.0 foundation — 2026-11-04

- New independent portable domain for sleep, capture classification and physical companion approval.
- Streaming PCM16LE WAV writer with bounded memory, recoverable partials and FAT rename regression tests.
- French companion frontend implementing explicit API contracts; backend integration pending.
- Hardware reference and bounded diagnostic compiling on installed ESP32 toolchain.
- Agent workflow, source references, OAuth provisioning study and IDE handoff documents.
- Independent review corrections: reopen pending recordings and signal screen-only sleep changes.

Not a release of the complete assistant firmware; no on-device application validation yet.
