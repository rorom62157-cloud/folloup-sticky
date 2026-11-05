#pragma once
#include "note_service.hpp"
#include <string>

namespace compagnon {
struct NoteHttpResponse { int status; std::string body; bool mutated = false; };
// Authorized requests only. Caller owns serialization with ALL NoteStore users.
// No SD mount/recovery and no authorization is performed by this dispatcher.
class NoteHttp {
public:
    explicit NoteHttp(NoteStore& store) : service_(store) {}
    // Local physical UI only; caller must hold the same store mutex as HTTP.
    NoteService& localService() { return service_; }
    NoteHttpResponse handle(const std::string& method, const std::string& path,
                            const std::string& query, const std::string& body);
private:
    NoteService service_;
};
}
