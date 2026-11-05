#include "note_store.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <utility>

using namespace compagnon;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)

struct MemoryFiles : NoteFiles {
    std::map<std::string, std::string> disk;
    int fail_step = 0, step = 0;
    bool after = false, short_write = false, read_failure = false;
    int writes = 0;
    bool fault() { return ++step == fail_step; }
    NoteIoResult read(const std::string& name, std::size_t limit, std::string& bytes) override {
        if (read_failure) return NoteIoResult::Error;
        const auto it = disk.find(name);
        if (it == disk.end()) return NoteIoResult::Missing;
        if (it->second.size() > limit) return NoteIoResult::Error;
        bytes = it->second; return NoteIoResult::Ok;
    }
    NoteIoResult remove(const std::string& name) override {
        const bool bad = fault();
        if (bad && !after) return NoteIoResult::Error;
        const bool existed = disk.erase(name) != 0;
        if (bad) return NoteIoResult::Error;
        return existed ? NoteIoResult::Ok : NoteIoResult::Missing;
    }
    NoteIoResult writeExclusive(const std::string& name, const std::string& bytes) override {
        ++writes;
        const bool bad = fault();
        if (bad && !after) return NoteIoResult::Error;
        if (disk.count(name)) return NoteIoResult::Exists;
        // Partial transfer/ENOSPC or sync/close failure; bytes may remain.
        disk[name] = bad && short_write ? bytes.substr(0, bytes.size() / 2) : bytes;
        return bad ? NoteIoResult::Error : NoteIoResult::Ok;
    }
    NoteIoResult renameAbsent(const std::string& from, const std::string& to) override {
        const bool bad = fault();
        if (bad && !after) return NoteIoResult::Error;
        if (disk.count(to)) return NoteIoResult::Exists; // FAT semantics.
        if (!disk.count(from)) return NoteIoResult::Missing;
        disk[to] = disk.at(from); disk.erase(from);
        return bad ? NoteIoResult::Error : NoteIoResult::Ok;
    }
    void faultsOff() { fail_step = 0; step = 0; after = false; short_write = false; }
};

void domain() {
    MemoryFiles files; NoteStore store(files); Note first;
    CHECK(store.create({"a", "b", false, ""}) == NoteResult::NotReady);
    CHECK(store.recover() == NoteResult::Ok);
    CHECK(store.create({"Idée", "Texte\nfrançais", false, "audio_123"}, &first) == NoteResult::Ok);
    CHECK(first.id == "n1" && first.version == 1);
    CHECK(store.update(first.id, {"new", "updated", false, ""}, 0) == NoteResult::Conflict);
    CHECK(store.setPinned(first.id, true, 1, &first) == NoteResult::Ok);
    CHECK(first.version == 2 && first.pinned && first.audio_id == "audio_123");
    NoteStore restart(files); CHECK(restart.recover() == NoteResult::Ok);
    CHECK(restart.find(first.id)->text == "Texte\nfrançais");
    CHECK(restart.find(first.id)->pinned);
    CHECK(restart.update(first.id, {"edited", "body", true, ""}, 2, &first) == NoteResult::Ok);
    CHECK(first.version == 3 && first.audio_id.empty());
    CHECK(restart.erase(first.id, 2) == NoteResult::Conflict);
    CHECK(restart.erase(first.id, 3) == NoteResult::Ok);
    NoteStore deleted(files); CHECK(deleted.recover() == NoteResult::Ok);
    CHECK(deleted.notes().empty());
    CHECK(deleted.create({"next", "", false, ""}, &first) == NoteResult::Ok);
    CHECK(first.id == "n5");
    CHECK(deleted.erase("n999", 1) == NoteResult::NotFound);
    CHECK(deleted.erase("../notes", 1) == NoteResult::InvalidArgument);
}
void bounds() {
    MemoryFiles files; NoteStore store(files); CHECK(store.recover() == NoteResult::Ok);
    CHECK(store.create({std::string(161, 'a'), "", false, ""}) == NoteResult::InvalidArgument);
    CHECK(store.create({"", std::string(4097, 'a'), false, ""}) == NoteResult::InvalidArgument);
    CHECK(store.create({"", "", false, "../secret"}) == NoteResult::InvalidArgument);
    CHECK(store.create({std::string("a\0b", 3), "", false, ""}) == NoteResult::InvalidArgument);
    CHECK(store.create({"\xc0\xaf", "", false, ""}) == NoteResult::InvalidArgument);
    CHECK(store.create({"\xed\xa0\x80", "", false, ""}) == NoteResult::InvalidArgument);
    CHECK(store.create({"\xf4\x90\x80\x80", "", false, ""}) == NoteResult::InvalidArgument);
    CHECK(store.create({"\xe2\x82", "", false, ""}) == NoteResult::InvalidArgument);
    CHECK(files.writes == 0);
    for (std::size_t i = 0; i < NoteStore::max_notes; ++i)
        CHECK(store.create({std::string(160, 't'), std::string(4096, 'x'), false, std::string(64, 'a')}) == NoteResult::Ok);
    CHECK(store.create({"one too many", "", false, ""}) == NoteResult::LimitExceeded);
    NoteStore restart(files); CHECK(restart.recover() == NoteResult::Ok);
    CHECK(restart.notes().size() == NoteStore::max_notes);
}
void interrupted() {
    MemoryFiles baseline; NoteStore initial(baseline); Note first;
    CHECK(initial.recover() == NoteResult::Ok);
    CHECK(initial.create({"old", "body", false, ""}, &first) == NoteResult::Ok);
    CHECK(initial.setPinned(first.id, true, 1) == NoteResult::Ok); // Both slots populated.
    for (int operation = 0; operation < 3; ++operation) {
        for (int fail = 1; fail <= 4; ++fail) {
            for (bool after : {false, true}) {
                MemoryFiles files = baseline; files.faultsOff();
                NoteStore store(files); CHECK(store.recover() == NoteResult::Ok);
                files.fail_step = fail; files.after = after; files.short_write = true;
                NoteResult result;
                if (operation == 0) result = store.update(first.id, {"new", "updated", false, ""}, 2);
                else if (operation == 1) result = store.erase(first.id, 2);
                else result = store.create({"second", "", false, ""});
                CHECK(result == NoteResult::IoError && !store.ready());
                CHECK(store.find(first.id) == nullptr && store.notes().empty());
                CHECK(store.create({"", "", false, ""}) == NoteResult::NotReady);
                files.faultsOff(); NoteStore reboot(files);
                CHECK(reboot.recover() == NoteResult::Ok);
                const bool committed = fail == 4 && after;
                if (!committed) CHECK(reboot.find(first.id)->title == "old" && reboot.find(first.id)->version == 2);
                else if (operation == 0) CHECK(reboot.find(first.id)->title == "new" && reboot.find(first.id)->version == 3);
                else if (operation == 1) CHECK(reboot.notes().empty());
                else CHECK(reboot.notes().size() == 2);
                // Leftover .part never blocks subsequent FAT-compatible saves.
                CHECK(reboot.create({"after recovery", "", false, ""}) == NoteResult::Ok);
            }
        }
    }
    // Flush/sync failure with complete bytes is still uncommitted.
    MemoryFiles files = baseline; files.faultsOff(); NoteStore store(files);
    CHECK(store.recover() == NoteResult::Ok);
    files.fail_step = 3; files.after = true;
    CHECK(store.erase(first.id, 2) == NoteResult::IoError);
    files.faultsOff(); CHECK(store.recover() == NoteResult::Ok);
    CHECK(store.find(first.id) != nullptr);
    // Interrupted very first save has no committed note, even if .part is valid.
    MemoryFiles empty; NoteStore fresh(empty); CHECK(fresh.recover() == NoteResult::Ok);
    empty.fail_step = 4;
    CHECK(fresh.create({"first", "", false, ""}) == NoteResult::IoError);
    empty.faultsOff(); CHECK(fresh.recover() == NoteResult::Ok && fresh.notes().empty());
}
void rollbackAba() {
    MemoryFiles files; NoteStore store(files); Note first, second;
    CHECK(store.recover() == NoteResult::Ok);
    CHECK(store.create({"first", "", false, ""}, &first) == NoteResult::Ok);
    CHECK(store.create({"original second", "", false, ""}, &second) == NoteResult::Ok);
    CHECK(first.id == "n1" && second.id == "n2" && second.version == 1);
    files.disk["notes-b.bin"][30] ^= 1;
    const auto preserved = files.disk;
    const int writes = files.writes;
    NoteStore reboot(files);
    CHECK(reboot.recover() == NoteResult::Corrupt && !reboot.ready());
    CHECK(reboot.notes().empty() && reboot.find(first.id) == nullptr);
    // No replacement n2/v1 can be created, so stale requests cannot delete it.
    CHECK(reboot.create({"unrelated replacement", "", false, ""}) == NoteResult::NotReady);
    CHECK(reboot.erase(second.id, second.version) == NoteResult::NotReady);
    CHECK(reboot.update(second.id, {"stale edit", "", false, ""}, second.version) == NoteResult::NotReady);
    CHECK(reboot.setPinned(second.id, true, second.version) == NoteResult::NotReady);
    CHECK(files.writes == writes && files.disk == preserved);
    // Corruption in the older slot also blocks writes rather than erasing evidence.
    files.disk = preserved;
    files.disk["notes-b.bin"] = files.disk["notes-a.bin"];
    files.disk["notes-a.bin"] = "corrupt older slot";
    const auto older_preserved = files.disk;
    CHECK(reboot.recover() == NoteResult::Corrupt && !reboot.ready());
    CHECK(reboot.create({"blocked", "", false, ""}) == NoteResult::NotReady);
    CHECK(files.disk == older_preserved);
}
void damaged() {
    MemoryFiles files; NoteStore store(files); Note first;
    CHECK(store.recover() == NoteResult::Ok);
    CHECK(store.create({"old", "", false, ""}, &first) == NoteResult::Ok);
    CHECK(store.update(first.id, {"new", "", false, ""}, 1) == NoteResult::Ok);
    const auto good = files.disk;
    // Every truncation fails closed even with an intact older committed slot.
    const auto newest = files.disk.at("notes-b.bin");
    for (std::size_t size = 0; size < newest.size(); ++size) {
        files.disk = good; files.disk["notes-b.bin"] = newest.substr(0, size);
        CHECK(store.recover() == NoteResult::Corrupt && !store.ready());
    }
    files.disk = good; files.disk["notes-b.bin"][30] ^= 1;
    CHECK(store.recover() == NoteResult::Corrupt && !store.ready());
    files.disk["notes-a.bin"] = "bad";
    const int writes = files.writes;
    CHECK(store.recover() == NoteResult::Corrupt && !store.ready());
    CHECK(store.create({"", "", false, ""}) == NoteResult::NotReady && files.writes == writes);
    files.disk = good; files.read_failure = true;
    CHECK(store.recover() == NoteResult::IoError && !store.ready());
    files.read_failure = false; files.disk["notes-a.bin"] = std::string(150001, 'x');
    CHECK(store.recover() == NoteResult::IoError); // Adapter fails closed before allocation.
}
int main() {
    domain(); bounds(); interrupted(); damaged(); rollbackAba();
    std::cout << "note store tests passed\n";
}
