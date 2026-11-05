#pragma once

#include "note_store.hpp"

namespace compagnon {
// Flat, bounded web mutation schema. title/text are required. Updates also
// require pinned/version; creates accept only title/text, never pinned/version/audio.
// Invalid input leaves both outputs unchanged. Success clears draft.audio_id;
// adapters preserve an existing audio link rather than accepting one from HTTP.
constexpr std::size_t max_note_json_bytes = 24576;
bool parseNoteJson(const std::string& body, bool update, NoteDraft& draft,
                   std::uint64_t& version);
// Notes are validated domain records from NoteStore. Optional audio_id uses the
// same spelling as the web API; encoding never embeds unescaped JSON controls.
std::string noteJson(const Note& note);
std::string notesJson(const std::vector<Note>& notes);
} // namespace compagnon
