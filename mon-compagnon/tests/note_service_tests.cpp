#include "note_service.hpp"

#include <cstdlib>
#include <iostream>
#include <map>

using namespace compagnon;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)

struct MemoryFiles final : NoteFiles {
    std::map<std::string, std::string> disk;
    bool fail_write = false;
    int writes = 0;
    NoteIoResult read(const std::string& name, std::size_t limit, std::string& bytes) override {
        const auto it = disk.find(name);
        if (it == disk.end()) return NoteIoResult::Missing;
        if (it->second.size() > limit) return NoteIoResult::Error;
        bytes = it->second; return NoteIoResult::Ok;
    }
    NoteIoResult writeExclusive(const std::string& name, const std::string& bytes) override {
        ++writes;
        if (fail_write) return NoteIoResult::Error;
        if (disk.count(name)) return NoteIoResult::Exists;
        disk[name] = bytes; return NoteIoResult::Ok;
    }
    NoteIoResult remove(const std::string& name) override {
        return disk.erase(name) ? NoteIoResult::Ok : NoteIoResult::Missing;
    }
    NoteIoResult renameAbsent(const std::string& from, const std::string& to) override {
        if (disk.count(to)) return NoteIoResult::Exists;
        const auto it = disk.find(from);
        if (it == disk.end()) return NoteIoResult::Missing;
        disk[to] = it->second; disk.erase(it); return NoteIoResult::Ok;
    }
};
void unavailable() {
    MemoryFiles files; NoteStore store(files); NoteService service(store);
    CHECK(service.list().status == 503 && service.list().notes.empty());
    CHECK(service.get("n1").status == 503);
    CHECK(service.create({"a", "b", std::nullopt}).status == 503);
    CHECK(service.update("n1", {"a", "b", false, 1, std::nullopt}).status == 503);
    CHECK(service.setPinned("n1", true, 1).status == 503);
    CHECK(service.erase("n1", 1).status == 503);
    CHECK(files.writes == 0);
}
void crud() {
    MemoryFiles files; NoteStore store(files); NoteService service(store);
    CHECK(store.recover() == NoteResult::Ok);
    CHECK(service.list().ok() && service.list().notes.empty());
    auto created = service.create({"Idée", "Texte français", std::nullopt});
    CHECK(created.status == 201 && created.note && created.note->version == 1);
    CHECK(!created.note->pinned && created.note->audio_id.empty());
    const auto id = created.note->id;
    CHECK(service.get(id).note->text == "Texte français");
    auto snapshot = service.list();
    auto updated = service.update(id, {"Modifiée", "Autre texte", true, 1, std::nullopt});
    CHECK(updated.status == 200 && updated.note->version == 2 && updated.note->pinned);
    CHECK(snapshot.notes.size() == 1 && snapshot.notes[0].title == "Idée");
    auto conflict = service.update(id, {"Écrasée", "", false, 1, std::nullopt});
    CHECK(conflict.status == 409 && conflict.error == "note_conflict" && !conflict.note);
    CHECK(service.erase(id, 1).status == 409);
    CHECK(service.setPinned(id, false, 1).status == 409);
    CHECK(service.get(id).note->title == "Modifiée");
    const auto pinned = service.setPinned(id, false, 2);
    CHECK(pinned.status == 200 && !pinned.note->pinned && pinned.note->version == 3);
    auto deleted = service.erase(id, 3);
    CHECK(deleted.status == 204 && deleted.ok() && !deleted.note && deleted.notes.empty());
    CHECK(service.get(id).status == 404);
    CHECK(service.erase(id, 3).status == 404);
    CHECK(service.setPinned(id, true, 3).status == 404);
    CHECK(service.update(id, {"gone", "", false, 3, std::nullopt}).status == 404);
    NoteStore reboot(files); CHECK(reboot.recover() == NoteResult::Ok);
    CHECK(reboot.notes().empty());
}
void audioAndValidation() {
    MemoryFiles files; NoteStore store(files); NoteService service(store); Note voice;
    CHECK(store.recover() == NoteResult::Ok);
    // Only the trusted recording workflow may attach an owned audio reference.
    CHECK(store.create({"Vocale", "Transcription", false, "recording_1"}, &voice) == NoteResult::Ok);
    const int writes = files.writes;
    CHECK(service.create({"forged", "", std::string("recording_1")}).status == 400);
    CHECK(service.create({"forged", "", std::string()}).status == 400);
    CHECK(service.update(voice.id, {"changed", "", false, 1, std::string()}).status == 400);
    CHECK(service.update(voice.id, {"changed", "", false, 1, std::string("recording_1")}).status == 400);
    CHECK(service.create({std::string(161, 'x'), "", std::nullopt}).status == 400);
    CHECK(service.create({"", std::string(4097, 'x'), std::nullopt}).status == 400);
    CHECK(service.create({"\xc0\xaf", "", std::nullopt}).status == 400);
    for (const auto version : {std::uint64_t(0), NoteStore::max_version + 1}) {
        CHECK(service.update(voice.id, {"changed", "", false, version, std::nullopt}).status == 400);
        CHECK(service.erase(voice.id, version).status == 400);
        CHECK(service.setPinned(voice.id, true, version).status == 400);
    }
    for (const auto& id : {std::string(), std::string("../n1"), std::string(33, 'n')}) {
        CHECK(service.get(id).status == 400);
        CHECK(service.erase(id, 1).status == 400);
        CHECK(service.setPinned(id, true, 1).status == 400);
        CHECK(service.update(id, {"", "", false, 1, std::nullopt}).status == 400);
    }
    CHECK(files.writes == writes);
    auto edit = service.update(voice.id, {"Corrigée", "Nouveau texte", true, 1, std::nullopt});
    CHECK(edit.status == 200 && edit.note->audio_id == "recording_1");
    auto pin = service.setPinned(voice.id, false, 2);
    CHECK(pin.status == 200 && pin.note->audio_id == "recording_1");
    NoteStore reboot(files); CHECK(reboot.recover() == NoteResult::Ok);
    CHECK(reboot.find(voice.id)->audio_id == "recording_1");
}
void limitsAndFaults() {
    MemoryFiles files; NoteStore store(files); NoteService service(store);
    CHECK(store.recover() == NoteResult::Ok);
    for (std::size_t i = 0; i < NoteStore::max_notes; ++i)
        CHECK(service.create({"note", "", std::nullopt}).status == 201);
    CHECK(service.create({"over limit", "", std::nullopt}).status == 507);
    const auto first = service.list().notes[0];
    files.fail_write = true;
    const auto failure = service.update(first.id, {"private title", "secret body", false, 1, std::nullopt});
    CHECK(failure.status == 503 && failure.result == NoteResult::IoError && !failure.note);
    CHECK(failure.message.find("secret") == std::string::npos);
    CHECK(!store.ready() && service.list().status == 503 && service.get(first.id).status == 503);
    CHECK(service.create({"retry", "", std::nullopt}).status == 503);
    files.fail_write = false;
    CHECK(store.recover() == NoteResult::Ok);
    CHECK(service.get(first.id).note->title == "note");
    // Corrupt snapshots fail recovery closed; the service exposes no old values.
    files.disk["notes-a.bin"] = "corrupt";
    CHECK(store.recover() == NoteResult::Corrupt);
    CHECK(service.list().status == 503 && service.list().notes.empty());
}
int main() {
    unavailable(); crud(); audioAndValidation(); limitsAndFaults();
    std::cout << "note service tests passed\n";
}
