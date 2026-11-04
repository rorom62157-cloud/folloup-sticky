#pragma once
#include "note_http.hpp"
#include "postit_store.hpp"
namespace compagnon {
// Authorized requests; caller serializes with every NoteStore and PostitStore user.
class PostitHttp {
public:
    PostitHttp(NoteStore& notes, PostitStore& styles) : notes_(notes), styles_(styles) {}
    PostitStore& localService() { return styles_; }
    NoteResult eraseForNote(const std::string& id) { return styles_.eraseForNote(id); }
    NoteHttpResponse handle(const std::string& method, const std::string& path,
                            const std::string& query, const std::string& body);
private:
    NoteStore& notes_; PostitStore& styles_;
};
}
