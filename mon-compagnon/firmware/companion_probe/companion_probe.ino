// COMPILE-ONLY TRANSPORT PROBE: never flash this harness as an application.
// No Wi-Fi setup. Hardware UI/SD adapters are deliberately not connected yet.
#include <Arduino.h>
#include "embedded_http.hpp"
#include "posix_note_files.hpp"
volatile bool compile_transport = false;
void setup() {
    if (compile_transport) {
        static compagnon::PosixNoteFiles files("/sdcard/compagnon-notes");
        static compagnon::NoteStore store(files);
        static compagnon::NoteHttp notes(store);
        // Compile-only: no mount or recover is attempted by this harness.
        static compagnon::EmbeddedHttp server("http://192.168.4.1", &notes);
        server.begin();
        server.pending();
        server.physicalDecision(false);
        server.revoke();
    }
}
void loop() { delay(1000); }
