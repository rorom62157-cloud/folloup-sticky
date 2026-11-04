#include "task_store.hpp"

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
    MemoryFiles files; TaskStore store(files); Task first;
    CHECK(!store.ready() && store.tasks().empty());
    CHECK(store.create("a", "b") == NoteResult::NotReady);
    CHECK(store.update("n1", "a", "b", 1) == NoteResult::NotReady);
    CHECK(store.setDone("n1", true, 1) == NoteResult::NotReady);
    CHECK(store.erase("n1", 1) == NoteResult::NotReady);
    CHECK(store.recover() == NoteResult::Ok);
    CHECK(store.create("Acheter du café", "Détails\nfrançais", &first) == NoteResult::Ok);
    CHECK(first.id == "n1" && first.version == 1 && !first.done);
    const auto original = store.tasks();
    CHECK(store.update(first.id, "new", "changed", 0) == NoteResult::Conflict);
    CHECK(store.setDone(first.id, true, 0) == NoteResult::Conflict);
    CHECK(store.setDone(first.id, true, 1, &first) == NoteResult::Ok);
    CHECK(first.done && first.version == 2 && first.details == "Détails\nfrançais");
    CHECK(original.size() == 1 && !original[0].done && original[0].version == 1);
    CHECK(store.update(first.id, "Café acheté", "Nouveaux détails", 2, &first) == NoteResult::Ok);
    CHECK(first.done && first.version == 3 && first.title == "Café acheté");
    TaskStore restart(files); CHECK(restart.recover() == NoteResult::Ok);
    const auto restored = restart.tasks();
    CHECK(restored.size() == 1 && restored[0].done && restored[0].version == 3);
    CHECK(restored[0].details == "Nouveaux détails");
    CHECK(restart.erase(first.id, 2) == NoteResult::Conflict);
    CHECK(restart.setDone(first.id, false, 3, &first) == NoteResult::Ok);
    CHECK(!first.done && first.version == 4 && first.title == "Café acheté");
    CHECK(restart.erase(first.id, 4) == NoteResult::Ok);
    CHECK(restart.tasks().empty() && restored.size() == 1);
    TaskStore deleted(files); CHECK(deleted.recover() == NoteResult::Ok);
    CHECK(deleted.tasks().empty());
    CHECK(deleted.create("Next", "", &first) == NoteResult::Ok && first.id == "n6");
    CHECK(deleted.update("n999", "missing", "", 1) == NoteResult::NotFound);
    CHECK(deleted.setDone("n999", true, 1) == NoteResult::NotFound);
    CHECK(deleted.erase("n999", 1) == NoteResult::NotFound);
    CHECK(deleted.update("../notes", "bad", "", 1) == NoteResult::InvalidArgument);
    CHECK(deleted.setDone("../notes", true, 1) == NoteResult::InvalidArgument);
    CHECK(deleted.erase("../notes", 1) == NoteResult::InvalidArgument);
}
void bounds() {
    MemoryFiles files; TaskStore store(files); CHECK(store.recover() == NoteResult::Ok);
    const std::string bad_titles[] = {"", " \t\r\n", "\xc2\xa0\xe3\x80\x80", std::string(161, 'a'),
        std::string("a\0b", 3), "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82"};
    Task unchanged{"sentinel", "saved", "output", true, 42};
    for (const auto& title : bad_titles) {
        CHECK(store.create(title, "", &unchanged) == NoteResult::InvalidArgument);
        CHECK(unchanged.id == "sentinel" && unchanged.version == 42);
    }
    CHECK(store.create("Title", std::string(4097, 'a')) == NoteResult::InvalidArgument);
    CHECK(store.create("Title", "\xe2\x82") == NoteResult::InvalidArgument);
    CHECK(files.writes == 0);
    Task first;
    CHECK(store.create("Valid", "details", &first) == NoteResult::Ok);
    const auto writes = files.writes;
    for (const auto& title : bad_titles)
        CHECK(store.update(first.id, title, "", 1) == NoteResult::InvalidArgument);
    CHECK(store.update(first.id, "valid", std::string(4097, 'a'), 1) == NoteResult::InvalidArgument);
    CHECK(files.writes == writes && store.tasks()[0].version == 1);
    for (std::size_t i = 1; i < TaskStore::max_tasks; ++i)
        CHECK(store.create(std::string(160, 't'), std::string(4096, 'x')) == NoteResult::Ok);
    CHECK(store.create("one too many", "") == NoteResult::LimitExceeded);
    TaskStore restart(files); CHECK(restart.recover() == NoteResult::Ok);
    CHECK(restart.tasks().size() == TaskStore::max_tasks);
}
void interrupted() {
    MemoryFiles baseline; TaskStore initial(baseline); Task first;
    CHECK(initial.recover() == NoteResult::Ok);
    CHECK(initial.create("old", "body", &first) == NoteResult::Ok);
    CHECK(initial.setDone(first.id, true, 1) == NoteResult::Ok);
    for (int operation = 0; operation < 4; ++operation) {
        for (int fail = 1; fail <= 4; ++fail) {
            for (bool after : {false, true}) {
                MemoryFiles files = baseline; files.faultsOff();
                TaskStore store(files); CHECK(store.recover() == NoteResult::Ok);
                files.fail_step = fail; files.after = after; files.short_write = true;
                Task unchanged{"sentinel", "", "", false, 42};
                NoteResult result;
                if (operation == 0) result = store.update(first.id, "new", "changed", 2, &unchanged);
                else if (operation == 1) result = store.erase(first.id, 2);
                else if (operation == 2) result = store.create("second", "", &unchanged);
                else result = store.setDone(first.id, false, 2, &unchanged);
                CHECK(result == NoteResult::IoError && !store.ready());
                CHECK(unchanged.id == "sentinel" && unchanged.version == 42);
                CHECK(store.tasks().empty());
                CHECK(store.create("blocked", "") == NoteResult::NotReady);
                files.faultsOff(); TaskStore reboot(files);
                CHECK(reboot.recover() == NoteResult::Ok);
                const auto tasks = reboot.tasks();
                const bool committed = fail == 4 && after;
                if (!committed) CHECK(tasks.size() == 1 && tasks[0].title == "old" && tasks[0].done && tasks[0].version == 2);
                else if (operation == 0) CHECK(tasks[0].title == "new" && tasks[0].done && tasks[0].version == 3);
                else if (operation == 1) CHECK(tasks.empty());
                else if (operation == 2) CHECK(tasks.size() == 2 && tasks[0].done && !tasks[1].done);
                else CHECK(!tasks[0].done && tasks[0].version == 3);
                CHECK(reboot.create("after recovery", "") == NoteResult::Ok);
            }
        }
    }
}
void corrupt() {
    MemoryFiles files; TaskStore store(files); Task first, second;
    CHECK(store.recover() == NoteResult::Ok);
    CHECK(store.create("first", "", &first) == NoteResult::Ok);
    CHECK(store.create("second", "", &second) == NoteResult::Ok);
    const auto good = files.disk;
    for (const auto& slot : {"notes-a.bin", "notes-b.bin"}) {
        files.disk = good; files.disk[slot][30] ^= 1;
        const auto preserved = files.disk;
        CHECK(store.recover() == NoteResult::Corrupt && !store.ready());
        CHECK(store.tasks().empty());
        CHECK(store.create("replacement", "") == NoteResult::NotReady);
        CHECK(store.erase(second.id, second.version) == NoteResult::NotReady);
        CHECK(store.update(first.id, "changed", "", 1) == NoteResult::NotReady);
        CHECK(store.setDone(first.id, true, 1) == NoteResult::NotReady);
        CHECK(files.disk == preserved);
    }
    files.disk = good; files.read_failure = true;
    CHECK(store.recover() == NoteResult::IoError && !store.ready());
    files.read_failure = false;
    CHECK(store.recover() == NoteResult::Ok && store.tasks().size() == 2);
    // Valid NoteStore bytes which violate the task domain also fail closed.
    for (bool audio : {false, true}) {
        MemoryFiles invalid;
        NoteStore raw(invalid); CHECK(raw.recover() == NoteResult::Ok);
        CHECK(raw.create({audio ? "Task" : " \t", "", false, audio ? "audio_1" : ""}) == NoteResult::Ok);
        const auto preserved = invalid.disk;
        TaskStore tasks(invalid);
        CHECK(tasks.recover() == NoteResult::Corrupt && !tasks.ready());
        CHECK(tasks.tasks().empty() && tasks.create("blocked", "") == NoteResult::NotReady);
        CHECK(invalid.disk == preserved);
    }
}
int main() {
    domain(); bounds(); interrupted(); corrupt();
    std::cout << "task store tests passed\n";
}
