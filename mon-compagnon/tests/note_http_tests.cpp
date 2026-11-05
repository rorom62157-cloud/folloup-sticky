#include "note_http.hpp"
#include "posix_note_files.hpp"
#include "http_access.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>
#include <cstdio>

using namespace compagnon;
int main() {
    char directory[] = "/private/tmp/compagnon-http-XXXXXX";
    assert(mkdtemp(directory));
    PosixNoteFiles files(directory); NoteStore store(files);
    assert(store.recover() == NoteResult::Ok); NoteHttp api(store);
    assert(api.handle("GET", "/api/notes", "", "").body == "{\"notes\":[]}");
    CompanionSession session; HttpAccess access(session, "http://device.local");
    const std::string token(32, 'a'), csrf(32, 'b'), cookie = "mc_session=" + token;
    HttpAccessRequest request{"POST", "/api/notes", "device.local", "http://device.local", "same-origin", cookie, csrf, 26, true};
    auto dispatch = [&](Millis now) {
        auto decision = access.check(request, now);
        return decision.allowed() ? api.handle(request.method, request.path, "", R"({"title":"Idée","text":"Un texte"})") : NoteHttpResponse{decision.status, "", false};
    };
    assert(dispatch(0).status == 401);
    assert(access.requestSession(token, csrf, 1) == Result::Ok);
    assert(dispatch(2).status == 401);
    assert(session.physicalApprove(3) == Result::Ok);
    request.csrf = "wrong"; assert(dispatch(4).status == 403);
    request.csrf = csrf; request.origin = "http://foreign.local"; assert(dispatch(5).status == 403);
    request.origin = "http://device.local";
    const auto created = dispatch(6); assert(created.status == 201 && created.mutated);
    assert(store.notes().size() == 1); const auto note = store.notes().front();
    const auto path = "/api/notes/" + note.id;
    assert(api.handle("GET", path, "", "").status == 200);
    const std::string update = R"({"title":"Été","text":"À garder","pinned":true,"version":)" + std::to_string(note.version) + "}";
    assert(api.handle("PATCH", path, "", update).status == 200);
    assert(api.handle("PATCH", path, "", update).status == 409);
    assert(api.handle("DELETE", path, "version=" + std::to_string(note.version), "").status == 409);
    const auto current = store.notes().front();
    for (const auto* query : {"", "version=0", "version=01", "version=1&version=2", "version=9007199254740992", "version=1.0", "version=%31"})
        assert(api.handle("DELETE", path, query, "").status == 400);
    assert(api.handle("GET", "/api/notes/../x", "", "").status == 400);
    assert(api.handle("POST", "/api/notes", "", std::string(24577, 'x')).status == 413);
    assert(api.handle("POST", path, "", "{}").status == 405);
    assert(api.handle("POST", "/api/notes", "extra=1", "{}").status == 400);
    assert(session.revoke(7) == Result::Ok); assert(dispatch(8).status == 401);
    NoteStore reboot(files); assert(reboot.recover() == NoteResult::Ok);
    assert(reboot.notes().size() == 1 && reboot.notes().front().text == "À garder");
    NoteHttp recovered(reboot);
    assert(recovered.handle("DELETE", path, "version=" + std::to_string(current.version), "").status == 204);
    assert(recovered.handle("GET", path, "", "").status == 404);
    for (const char* name : {"notes-a.bin", "notes-b.bin", "notes-next.part"}) std::remove((std::string(directory) + "/" + name).c_str());
    assert(rmdir(directory) == 0);
    std::cout << "note HTTP integration tests passed\n";
}
