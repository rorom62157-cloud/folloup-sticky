#include "note_http.hpp"
#include "note_json.hpp"

namespace compagnon {
namespace {
NoteHttpResponse error(int status, const char* code, const char* message) {
    return {status, std::string("{\"error\":\"") + code + "\",\"message\":\"" + message + "\"}", false};
}
bool versionQuery(const std::string& query, std::uint64_t& value) {
    if (query.rfind("version=", 0) != 0 || query.size() == 8) return false;
    value = 0;
    for (std::size_t i = 8; i < query.size(); ++i) {
        const char c = query[i];
        if (c < '0' || c > '9' || (i == 8 && c == '0')) return false;
        const auto digit = static_cast<unsigned>(c - '0');
        if (value > (NoteStore::max_version - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return value != 0;
}
NoteHttpResponse response(const NoteServiceResponse& r, bool mutation) {
    if (!r.ok()) return {r.status, "{\"error\":\"" + r.error + "\",\"message\":\"" + r.message + "\"}", false};
    if (r.status == 204) return {204, {}, mutation};
    return {r.status, r.note ? noteJson(*r.note) : notesJson(r.notes), mutation};
}
}
NoteHttpResponse NoteHttp::handle(const std::string& method, const std::string& path,
                                const std::string& query, const std::string& body) {
    if (body.size() > 24576) return error(413, "too_large", "Requête trop volumineuse.");
    const bool collection = path == "/api/notes";
    if (!collection && path.rfind("/api/notes/", 0) != 0)
        return error(404, "not_found", "Route inconnue.");
    const auto id = collection ? std::string{} : path.substr(11);
    if (!collection && (id.empty() || id.size() > 64 || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos))
        return error(400, "invalid_id", "Identifiant invalide.");
    if (method == "GET") {
        if (!body.empty() || !query.empty()) return error(400, "invalid_request", "Requête invalide.");
        return response(collection ? service_.list() : service_.get(id), false);
    }
    if (method == "DELETE" && !collection) {
        std::uint64_t version = 0;
        if (!body.empty() || !versionQuery(query, version)) return error(400, "invalid_version", "Version exacte requise.");
        return response(service_.erase(id, version), true);
    }
    if ((method == "POST" && collection) || (method == "PATCH" && !collection)) {
        NoteDraft draft; std::uint64_t version = 0;
        if (!query.empty() || !parseNoteJson(body, method == "PATCH", draft, version))
            return error(400, "invalid_note", "Note ou version invalide.");
        return response(collection ? service_.create({draft.title, draft.text, std::nullopt}) :
            service_.update(id, {draft.title, draft.text, draft.pinned, version, std::nullopt}), true);
    }
    return error(405, "method_not_allowed", "Méthode non autorisée.");
}
}
