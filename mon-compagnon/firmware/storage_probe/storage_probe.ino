// COMPILE-ONLY, NEVER FLASH. No mount, format, SD rail or hardware setup.
#include <Arduino.h>
#include "posix_note_files.hpp"
volatile bool compile_storage = false;
void setup() {
    if (compile_storage) {
        compagnon::PosixNoteFiles files("/sdcard/compagnon-notes");
        compagnon::NoteStore notes(files);
        notes.recover();
        notes.create({"compile", "only", false, ""});
        std::string bytes;
        files.read("notes-a.bin", compagnon::NoteStore::max_snapshot_bytes, bytes);
        files.writeExclusive("notes-next.part", bytes);
        files.renameAbsent("notes-next.part", "notes-b.bin");
        files.remove("notes-next.part");
    }
}
void loop() { delay(1000); }
