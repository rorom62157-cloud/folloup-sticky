# Persistent notes and post-it flags

`core/note_store.hpp/.cpp` provides portable C++17 storage policy. It does not access SD, expose HTTP routes, authorize callers, or delete audio. Firmware/HTTP adapters are still required. Access must be serialized by one application owner.

## Domain contract

A note contains `id`, `title`, `text`, `pinned`, `version` and optional `audio_id` (empty in C++ means absent in JSON). Create returns a generated opaque ID and version 1. Update replaces the supplied editable fields, including the audio association; adapters implementing partial PATCH must first merge omitted fields with the current note. Every update or pin operation increments that note's version, including a no-op update. Update, pin and delete require the expected version and return `Conflict` on mismatch. Unrelated note mutations do not increment this note's version. Several notes can be pinned; selecting the foreground post-it belongs to application/UI policy.

Limits: 32 notes, title 160 UTF-8 bytes, text 4096 UTF-8 bytes, audio ID 64 ASCII characters. Audio IDs and note IDs accept only ASCII letters, digits, `_` and `-`; they are references, never paths. Text rejects invalid UTF-8, NUL and ASCII controls except tab/newline/carriage return. Empty titles and texts are allowed. Versions/generations never exceed JavaScript's exact integer range (2^53−1). An adapter must enforce request-body bounds before constructing drafts. There are no settings or secret fields in this format; user-entered note content remains private personal data.

`recover()` must succeed before reading/mutating notes. `find()` returns null and `notes()` is empty after a recovery or mutation I/O failure. Returned references/pointers are invalidated by recovery and mutation. Map `Conflict` to web 409; successful create/update return the saved `Note`. Authorization, CSRF and physical confirmation remain adapter responsibilities.

## Files and commit sequence

Only three fixed basenames are ever passed to the injected `NoteFiles` interface:

| Name | Purpose |
| --- | --- |
| `notes-a.bin` | One complete committed snapshot |
| `notes-b.bin` | The other complete committed snapshot |
| `notes-next.part` | Uncommitted candidate, ignored during recovery |

Each binary snapshot uses magic/schema `MCNOTE01`, little-endian generation (u64), note count (u32), length-prefixed UTF-8 fields (u32 byte length), pinned byte (0/1), note version (u64), and trailing IEEE CRC32 over all preceding bytes. Fields per note are ID/title/text/pinned/version/audio ID. All lengths, counts, flags, versions, duplicate IDs and exact end-of-record position are checked before accepting a snapshot. CRC detects accidental damage; it is not authentication or encryption. Unknown/corrupt files are never interpreted as an empty store when no valid committed snapshot remains. No format migration is implemented.

For a mutation, construct the bounded next snapshot, remove stale `.part`, remove the slot other than the selected current snapshot, exclusively write/sync/close `.part`, then rename it to that absent slot. This uses FAT-compatible non-replacing rename. The selected current snapshot is never removed. The in-memory state changes only after successful rename. Deletion is persisted by omission from a full committed snapshot, so no separate tombstone is needed.

Recovery selects the valid committed snapshot with highest generation. It ignores `.part`, even if complete and checksummed. Any existing damaged committed snapshot produces `Corrupt` and leaves the store unavailable, even when the other snapshot is intact. Falling back to an earlier generation would allow reuse of IDs/versions and could let a stale request modify unrelated replacement content (an ABA conflict). Two valid but different snapshots with the same generation fail closed. Two missing committed files mean an empty new store; one missing file is accepted only when the other committed snapshot is valid. A read error fails closed instead of risking overwrite of an unread generation. Corrupt files are never repaired, removed or overwritten by the store. Explicit salvage with preserved originals and a safe identity/revision policy belongs to a later recovery tool; no such tool is implemented here.

## Adapter requirements and honest durability boundary

`NoteFiles` must own a private pre-created directory, forbid concurrent/external writers and symlink traversal, and scope the fixed basenames to that directory. No path is supplied by note content. `read` must check file size against `max_snapshot_bytes` (150000) before allocating and return I/O error for unavailable/oversized files. `Missing` means confirmed absence, not SD unavailable, permission failure or an unreadable file.

`writeExclusive` must fail if its target exists, write every byte (short writes are errors), flush, fsync using a supported VFS operation, and close successfully before returning `Ok`. On failure it may leave a partial file. `renameAbsent` must reject an existing destination and never replace it; `remove` distinguishes absence from errors. No operation automatically formats storage or removes an audio original. The adapter must not claim success when its required durability operation is unsupported.

On any mutation I/O error the store becomes unavailable until recovery. A rename may have completed even when its result is an error; recovery can therefore show either the old or the new complete state. Reconcile the recovered list before retrying a create to avoid a duplicate. A fully synced candidate without successful publication is ignored.

The algorithm keeps a previous complete snapshot while writing and provides tested interruption recovery **under the injected filesystem contract**. FAT metadata persistence, directory fsync, SD controller caching, filesystem corruption and actual brownouts are not validated. A successful rename is not a universal power-loss guarantee. Detected corruption blocks further use instead of rolling back into a mutable earlier generation. Complete loss of directory entries is outside this detection guarantee; the adapter/product must report storage damage honestly. This is not a backup system. Future schema migration must preserve originals and prevent older firmware from overwriting a newer format; it is a separate requirement before format changes ship.

Memory is bounded but uses dynamic C++ strings/vectors and full snapshots: at most 32 bounded notes, with temporary copies/serialized bytes during mutation/recovery. The 150000-byte serialized cap is not a peak heap guarantee; measure available PSRAM and allocation failures in the device adapter before device acceptance.

## Checks executed

`./scripts/test-notes.sh` compiles with clang++ C++17, `-Wall -Wextra -Werror -pedantic`, then runs the actual core against injected in-memory FAT-like files. Coverage: restart after create/edit/pin/delete, stale revision conflicts, ID non-reuse across normal deletion/restart, invalid IDs/text and UTF-8, maximum-size/full stores, every mutation I/O boundary before and after filesystem effects, short writes/ENOSPC-style partial data, complete-write sync/close failure, rename-error-after-publication ambiguity, initial-save interruption, stale `.part` cleanup, every truncation of the newest snapshot, checksum damage with either committed slot affected, concrete n2/version1 stale-delete ABA prevention, both snapshots invalid, read failures and oversized files. Tests assert old-or-new complete state for interrupted operations and deny all reads/mutations after detected committed-file corruption or I/O failure, preserving corrupt files unchanged.

The same suite also passed an AddressSanitizer/UndefinedBehaviorSanitizer build (`-fsanitize=address,undefined -fno-omit-frame-pointer`).

The initial fault fixture models required filesystem operations; it does not prove a particular SD driver meets them.

## Concrete filesystem adapter — subsequent slice

`firmware/storage/posix_note_files.*` now implements `NoteFiles` against POSIX/ESP-IDF FAT VFS. Supply an already mounted, pre-created absolute private directory such as `/sdcard/compagnon/notes`. The adapter does not initialize SD, create directories, format media or configure power. One application owner must serialize access and prevent external writers/unmounts throughout operations. ESP FAT has no symlinks; host builds check all path components and use no-follow opens. See `firmware/storage/README.md` for boundaries.

`sh scripts/test-note-files.sh` tests the real filesystem plus NoteStore restart, bounded files, unavailable bases, file/parent symlinks, exclusive writes, non-replacing publication and an actual host write failure. Strict C++17 and sanitizer runs passed. `sh scripts/build-storage-probe.sh` compiled/linked against Arduino ESP32 3.3.8 (325418 program bytes, 22628 static RAM). This harness is compile-only and MUST NOT be flashed. Independent review found no confirmed defects. Actual FAT fsync, media removal, brownouts and device heap remain unvalidated; no on-device persistence is claimed.
