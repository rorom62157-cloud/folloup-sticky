# Notes on an already mounted FAT volume

`PosixNoteFiles` implements `NoteFiles` using ESP-IDF VFS/POSIX calls. Pass an
absolute path to a private, pre-created directory on the mounted FAT volume
(for example `/sdcard/compagnon-notes`). Construction does no I/O. No code here
mounts, creates directories, formats, changes rails, or calls board drivers.

One application owner must serialize the complete NoteStore recovery/mutation
sequence, not just individual adapter calls. No other task, USB host, network
file service or process may write or replace that directory or its parents.
Keep the volume mounted throughout an operation. POSIX rename checks destination
absence before publication; because generic POSIX rename may replace, the
single-owner/no-external-writers requirement is necessary, not advisory.

Only `notes-a.bin`, `notes-b.bin` and `notes-next.part` are accepted. Paths reject
NUL, relative paths and dot components. Host builds reject symlink components
and symlink/non-regular files. ESP builds use `stat` because their VFS lacks
`lstat`; support there is restricted to FAT, which has no symlinks. This is not
a security sandbox against a concurrent writer changing paths between checks.

A missing or invalid directory returns Error, never an empty new store. Reads
inspect the opened file size before allocating snapshot content, enforce both
the caller bound and 150000-byte store bound, and reject incomplete/extra data
and close failures. Exclusive writes check full fwrite, fflush, fsync and
fclose, including failures on unsupported sync operations. Partial candidates
can remain after errors; NoteStore recovery is required before further use.
There is no fallback that claims success without fsync.

Successful file sync and rename do not establish brownout safety: FAT directory
metadata, SD controller caching, removal/unmount, actual power loss and heap
headroom still need device validation. Directory fsync is not performed or
claimed. Preserve originals if salvage is later needed.

`./scripts/test-note-files.sh` exercises the actual host filesystem, including
a host file-size-limit write failure; it does not inject filesystem mocks.
`./scripts/build-storage-probe.sh` compiles and links against the installed
ESP32-S3 Arduino/ESP-IDF runtime. The probe is compile-only, NEVER FLASH, and
contains no mounted-card or peripheral initialization. It cannot validate FAT
fsync support at runtime or device storage reliability.
