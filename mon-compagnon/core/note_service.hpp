#pragma once

#include "note_store.hpp"
#include <optional>

namespace compagnon {

// Presence of audio_id is represented separately from its value. Web callers
// cannot create, replace or remove audio bindings, even by passing an empty ID.
struct WebNoteCreate {
    std::string title, text;
    std::optional<std::string> audio_id;
};
struct WebNoteUpdate {
    std::string title, text;
    bool pinned = false;
    std::uint64_t version = 0;
    std::optional<std::string> audio_id;
};
struct NoteServiceResponse {
    NoteResult result = NoteResult::Ok;
    int status = 200;
    std::string error, message;
    std::optional<Note> note;
    std::vector<Note> notes;
    bool ok() const { return result == NoteResult::Ok; }
};

// Portable typed web API, with bounded records owned by NoteStore. Caller must
// serialize the ENTIRE call with every other store user and recover the store
// before use. This class provides NO authorization: transport must first enforce
// physical session approval, origin and CSRF checks. Transport also bounds body
// bytes, strictly decodes JSON types/required fields (including positive exact
// integer versions), and rejects unknown fields before constructing requests.
// Responses own their values and can be serialized after releasing the lock.
// Errors never include supplied note content or filesystem information. After an
// I/O error, recover externally; a mutation may have committed. Never retry a
// create automatically. This service cannot authorize an audio ownership link.
class NoteService {
public:
    explicit NoteService(NoteStore& store) : store_(store) {}
    NoteServiceResponse list() const;
    NoteServiceResponse get(const std::string& id) const;
    NoteServiceResponse create(const WebNoteCreate& request);
    NoteServiceResponse update(const std::string& id, const WebNoteUpdate& request);
    NoteServiceResponse setPinned(const std::string& id, bool pinned,
                                  std::uint64_t version);
    NoteServiceResponse erase(const std::string& id, std::uint64_t version);
private:
    NoteStore& store_;
};
} // namespace compagnon
