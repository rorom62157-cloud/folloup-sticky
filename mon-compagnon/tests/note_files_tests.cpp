#include "posix_note_files.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/resource.h>
#include <csignal>
#include <unistd.h>

using namespace compagnon;
namespace fs = std::filesystem;
struct Temp {
    std::string path;
    Temp() {
        char pattern[] = "/tmp/compagnon-notes-XXXXXX";
        char* made = ::mkdtemp(pattern);
        assert(made);
        char* physical = ::realpath(made, nullptr);
        assert(physical);
        path = physical;
        std::free(physical);
    }
    ~Temp() { fs::remove_all(path); }
};
void put(const std::string& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    file << bytes;
    file.close();
    assert(file);
}
int main() {
    Temp temp;
    const std::string base = temp.path + "/private";
    assert(fs::create_directory(base));
    PosixNoteFiles files(base);
    std::string out = "old";
    assert(files.read("notes-a.bin", 10, out) == NoteIoResult::Missing && out.empty());
    assert(files.remove("notes-a.bin") == NoteIoResult::Missing);
    assert(files.renameAbsent("notes-a.bin", "notes-b.bin") == NoteIoResult::Missing);
    for (const std::string name : {"../notes-a.bin", "/notes-a.bin", "notes-c.bin", "", "notes-a.bin/x"}) {
        assert(files.read(name, 10, out) == NoteIoResult::Error);
        assert(files.writeExclusive(name, "x") == NoteIoResult::Error);
        assert(files.remove(name) == NoteIoResult::Error);
        assert(files.renameAbsent("notes-a.bin", name) == NoteIoResult::Error);
    }
    assert(files.writeExclusive("notes-next.part", "abc") == NoteIoResult::Ok);
    assert(files.writeExclusive("notes-next.part", "replace") == NoteIoResult::Exists);
    assert(files.read("notes-next.part", 3, out) == NoteIoResult::Ok && out == "abc");
    assert(files.read("notes-next.part", 2, out) == NoteIoResult::Error && out.empty());
    assert(files.renameAbsent("notes-next.part", "notes-a.bin") == NoteIoResult::Ok);
    assert(files.writeExclusive("notes-next.part", "xyz") == NoteIoResult::Ok);
    assert(files.renameAbsent("notes-next.part", "notes-a.bin") == NoteIoResult::Exists);
    assert(files.read("notes-a.bin", 3, out) == NoteIoResult::Ok && out == "abc");
    assert(files.remove("notes-a.bin") == NoteIoResult::Ok);
    assert(files.remove("notes-next.part") == NoteIoResult::Ok);
    assert(files.writeExclusive("notes-a.bin", "") == NoteIoResult::Ok);
    assert(files.read("notes-a.bin", 0, out) == NoteIoResult::Ok && out.empty());
    assert(files.remove("notes-a.bin") == NoteIoResult::Ok);

    put(base + "/notes-a.bin", "x");
    fs::resize_file(base + "/notes-a.bin", NoteStore::max_snapshot_bytes + 1);
    assert(files.read("notes-a.bin", NoteStore::max_snapshot_bytes + 10, out) == NoteIoResult::Error);
    assert(files.writeExclusive("notes-b.bin", std::string(NoteStore::max_snapshot_bytes + 1, 'x')) == NoteIoResult::Error);
    assert(!fs::exists(base + "/notes-b.bin"));
    assert(files.remove("notes-a.bin") == NoteIoResult::Ok);

    // File and parent symlinks must never reach another location.
    put(temp.path + "/outside", "preserve");
    assert(::symlink((temp.path + "/outside").c_str(), (base + "/notes-a.bin").c_str()) == 0);
    assert(files.read("notes-a.bin", 100, out) == NoteIoResult::Error);
    assert(files.writeExclusive("notes-a.bin", "overwrite") == NoteIoResult::Error);
    assert(files.remove("notes-a.bin") == NoteIoResult::Error);
    assert(files.renameAbsent("notes-next.part", "notes-a.bin") == NoteIoResult::Error);
    fs::remove(base + "/notes-a.bin");
    assert(::symlink((temp.path + "/absent").c_str(), (base + "/notes-a.bin").c_str()) == 0);
    assert(files.read("notes-a.bin", 100, out) == NoteIoResult::Error);
    fs::remove(base + "/notes-a.bin");
    assert(::symlink(base.c_str(), (temp.path + "/alias").c_str()) == 0);
    PosixNoteFiles linked(temp.path + "/alias");
    assert(linked.writeExclusive("notes-a.bin", "x") == NoteIoResult::Error);
    assert(fs::create_directory(base + "/child"));
    PosixNoteFiles parentLink(temp.path + "/alias/child");
    assert(parentLink.read("notes-a.bin", 100, out) == NoteIoResult::Error);
    assert(fs::create_directory(base + "/notes-a.bin"));
    assert(files.read("notes-a.bin", 100, out) == NoteIoResult::Error);
    assert(files.remove("notes-a.bin") == NoteIoResult::Error);
    fs::remove(base + "/notes-a.bin");

    // An actual host write failure, without replacing the filesystem calls.
    struct rlimit original {};
    assert(::getrlimit(RLIMIT_FSIZE, &original) == 0);
    struct rlimit limited = original;
    limited.rlim_cur = 1024;
    auto previous = std::signal(SIGXFSZ, SIG_IGN);
    assert(::setrlimit(RLIMIT_FSIZE, &limited) == 0);
    const auto failed = files.writeExclusive("notes-next.part", std::string(8192, 'x'));
    assert(::setrlimit(RLIMIT_FSIZE, &original) == 0);
    std::signal(SIGXFSZ, previous);
    assert(failed == NoteIoResult::Error);
    assert(files.remove("notes-next.part") == NoteIoResult::Ok);

    Note saved;
    {
        NoteStore store(files);
        assert(store.recover() == NoteResult::Ok);
        assert(store.create({"Titre", "Texte é\n", false, "audio_1"}, &saved) == NoteResult::Ok);
        assert(store.setPinned(saved.id, true, saved.version, &saved) == NoteResult::Ok);
    }
    {
        PosixNoteFiles restarted(base);
        NoteStore store(restarted);
        assert(store.recover() == NoteResult::Ok);
        assert(store.notes().size() == 1);
        const auto* note = store.find(saved.id);
        assert(note && note->title == "Titre" && note->text == "Texte é\n" && note->pinned && note->audio_id == "audio_1");
        assert(store.erase(saved.id, saved.version) == NoteResult::Ok);
    }
    NoteStore empty(files);
    assert(empty.recover() == NoteResult::Ok && empty.notes().empty());
    const auto moved = temp.path + "/unavailable";
    fs::rename(base, moved);
    assert(files.read("notes-a.bin", 100, out) == NoteIoResult::Error);
    assert(files.remove("notes-a.bin") == NoteIoResult::Error);
    assert(files.writeExclusive("notes-a.bin", "x") == NoteIoResult::Error);
    assert(files.renameAbsent("notes-a.bin", "notes-b.bin") == NoteIoResult::Error);
    assert(empty.recover() == NoteResult::IoError && !empty.ready());
    PosixNoteFiles invalid(temp.path + "/unavailable/../unavailable");
    assert(invalid.read("notes-a.bin", 100, out) == NoteIoResult::Error);
    std::cout << "note_files_tests: real filesystem checks passed\n";
}
