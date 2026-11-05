#include "note_service.hpp"
#include <utility>

namespace compagnon {
namespace {
bool validId(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
bool validVersion(std::uint64_t version) {
    return version > 0 && version <= NoteStore::max_version;
}
NoteServiceResponse response(NoteResult result, int success = 200) {
    NoteServiceResponse out;
    out.result = result;
    switch (result) {
    case NoteResult::Ok: out.status = success; break;
    case NoteResult::NotReady:
        out.status = 503; out.error = "notes_unavailable";
        out.message = "Le stockage des notes est indisponible."; break;
    case NoteResult::InvalidArgument:
        out.status = 400; out.error = "invalid_note";
        out.message = "La note ou sa version est invalide."; break;
    case NoteResult::LimitExceeded:
        out.status = 507; out.error = "notes_limit";
        out.message = "La limite de stockage des notes est atteinte."; break;
    case NoteResult::NotFound:
        out.status = 404; out.error = "note_not_found";
        out.message = "Cette note est introuvable."; break;
    case NoteResult::Conflict:
        out.status = 409; out.error = "note_conflict";
        out.message = "Cette note a changé. Rechargez sa version actuelle."; break;
    case NoteResult::IoError:
        out.status = 503; out.error = "notes_io_error";
        out.message = "La sauvegarde est incertaine. Récupérez le stockage avant de réessayer."; break;
    case NoteResult::Corrupt:
        out.status = 503; out.error = "notes_corrupt";
        out.message = "Le stockage des notes nécessite une récupération."; break;
    }
    return out;
}
NoteServiceResponse savedResponse(NoteResult result, Note saved, int success = 200) {
    auto out = response(result, success);
    if (out.ok()) out.note = std::move(saved);
    return out;
}
} // namespace

NoteServiceResponse NoteService::list() const {
    if (!store_.ready()) return response(NoteResult::NotReady);
    auto out = response(NoteResult::Ok);
    out.notes = store_.notes();
    return out;
}
NoteServiceResponse NoteService::get(const std::string& id) const {
    if (!store_.ready()) return response(NoteResult::NotReady);
    if (!validId(id)) return response(NoteResult::InvalidArgument);
    const auto* note = store_.find(id);
    return note ? savedResponse(NoteResult::Ok, *note) : response(NoteResult::NotFound);
}
NoteServiceResponse NoteService::create(const WebNoteCreate& request) {
    if (!store_.ready()) return response(NoteResult::NotReady);
    if (request.audio_id.has_value()) return response(NoteResult::InvalidArgument);
    Note saved;
    const auto result = store_.create({request.title, request.text, false, ""}, &saved);
    return savedResponse(result, std::move(saved), 201);
}
NoteServiceResponse NoteService::update(const std::string& id, const WebNoteUpdate& request) {
    if (!store_.ready()) return response(NoteResult::NotReady);
    if (!validId(id) || !validVersion(request.version) || request.audio_id.has_value())
        return response(NoteResult::InvalidArgument);
    const auto* current = store_.find(id);
    if (!current) return response(NoteResult::NotFound);
    // Copy before mutation: committing invalidates NoteStore's old references.
    NoteDraft draft{request.title, request.text, request.pinned, current->audio_id};
    Note saved;
    const auto result = store_.update(id, draft, request.version, &saved);
    return savedResponse(result, std::move(saved));
}
NoteServiceResponse NoteService::setPinned(const std::string& id, bool pinned,
                                          std::uint64_t version) {
    if (!store_.ready()) return response(NoteResult::NotReady);
    if (!validId(id) || !validVersion(version)) return response(NoteResult::InvalidArgument);
    Note saved;
    const auto result = store_.setPinned(id, pinned, version, &saved);
    return savedResponse(result, std::move(saved));
}
NoteServiceResponse NoteService::erase(const std::string& id, std::uint64_t version) {
    if (!store_.ready()) return response(NoteResult::NotReady);
    if (!validId(id) || !validVersion(version)) return response(NoteResult::InvalidArgument);
    return response(store_.erase(id, version), 204);
}
} // namespace compagnon
