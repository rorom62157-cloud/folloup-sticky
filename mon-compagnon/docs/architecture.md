# Architecture — lot 1

The portable domain is separated from hardware and HTTP transport. It is testable now; this does not imply device execution.

## Domain interfaces

`SleepPolicy` takes monotonic milliseconds, last user activity, a foreground-post-it flag and bounded blockers. It emits state/screen transitions. Automatic light retains screen; manual light selects clock or post-it; deep selects post-it/brand. Physical wake requests sync; technical wake does not. The hardware adapter must wait for display completion, flush writes, disable radios and configure wake sources before entering deep sleep. Root has not implemented this adapter yet.

`RecordingWorkflow` emits capture/saving/menu/pending/proposal/deletion states. Audio samples and filesystem writes are adapter-owned. A saved pending original can reopen menus without recapture. `ActionRequested` means a proposed operation, not permission to execute Google or Gemini. External mutations require a separate confirmation and durable operation journal.

`CompanionSession` authorizes one unpredictable token after a physical event. No getters disclose token. Browser polling cannot renew idle expiration. HTTP transport must enforce cookies, same-origin/CSRF, body limits and session checks on every private route. The C++ type is not itself a web server.

## Web client

Vanilla HTML/CSS/JS calls the versioned API in contracts.md. It never implements authorization by hiding HTML alone. All private content is loaded after approval and cleared upon session loss. Backend does not yet exist in this lot. No browser-only switch can authorize a real session.

## Planned adapters

Board initialization and PMIC, e-paper rendering, audio SD streaming, durable data/index/outbox, Wi-Fi/server/session transport, verified-TLS Gemini and Google transports. The first adapter must demonstrate a full local capture -> save -> classify -> reopen path before cloud actions.

## Source and framework

Reference board pin mappings are in hardware.md; do not mix Seeed main with Waveshare branch. New domain source is independent. ESP-IDF production framework remains preferred; installed Arduino 3.3.8 is used only for isolated diagnostic compilation. No wholesale copy of upstream app; any future low-level reuse preserves license and provenance.

## WAV storage

WavWriter writes PCM16LE mono16kHz with <=4096-byte writes, 240-second sample cap, exclusive .part creation, final header/flush/fsync/close and rename only to an absent destination. The caller owns/serializes the directory. Initial POSIX placeholder reservation was rejected during review because FatFS rename does not replace existing destinations. Tests inject FAT-like rename behavior, collisions, short writes and fsync errors. stat is used on ESP_PLATFORM. Directory metadata is not fsynced: no universal power-loss durability claim. Index only after finish success; partial recovery is a later adapter.
