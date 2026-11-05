#include "postit_store.hpp"
#include <cstdlib>
#include <iostream>
#include <map>
using namespace compagnon;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)
struct MemoryFiles : NoteFiles {
    std::map<std::string, std::string> disk;
    bool read_error = false, write_error = false, rename_after_error = false;
    NoteIoResult read(const std::string& name, std::size_t max, std::string& bytes) override {
        if (read_error) return NoteIoResult::Error;
        const auto found = disk.find(name);
        if (found == disk.end()) return NoteIoResult::Missing;
        if (found->second.size() > max) return NoteIoResult::Error;
        bytes = found->second; return NoteIoResult::Ok;
    }
    NoteIoResult remove(const std::string& name) override { return disk.erase(name) ? NoteIoResult::Ok : NoteIoResult::Missing; }
    NoteIoResult writeExclusive(const std::string& name, const std::string& bytes) override {
        if (write_error) { disk[name] = bytes.substr(0, bytes.size()/2); return NoteIoResult::Error; }
        if (disk.count(name)) return NoteIoResult::Exists;
        disk[name] = bytes; return NoteIoResult::Ok;
    }
    NoteIoResult renameAbsent(const std::string& from, const std::string& to) override {
        if (disk.count(to)) return NoteIoResult::Exists;
        if (!disk.count(from)) return NoteIoResult::Missing;
        disk[to] = disk.at(from); disk.erase(from);
        return rename_after_error ? NoteIoResult::Error : NoteIoResult::Ok;
    }
};
void lifecycle() {
    MemoryFiles files; PostitStore store(files); PostitStyle style;
    CHECK(style.alignment == PostitAlignment::Center);
    CHECK(store.get("n1", style) == NoteResult::NotReady);
    CHECK(store.recover() == NoteResult::Ok && store.ready());
    CHECK(store.get("n1", style) == NoteResult::NotFound);
    CHECK(store.put("n1", PostitAlignment::Left, {{{0,0}, {1023,1023}}, {{5,0}}}, 0, &style) == NoteResult::Ok);
    CHECK(style.note_id == "n1" && style.version == 1 && style.strokes.size() == 2);
    CHECK(store.put("n1", PostitAlignment::Right, {}, 0) == NoteResult::Conflict);
    CHECK(store.put("n1", PostitAlignment::Right, {}, 2) == NoteResult::Conflict);
    CHECK(store.put("absent", PostitAlignment::Right, {}, 1) == NoteResult::NotFound);
    PostitStore restart(files); CHECK(restart.recover() == NoteResult::Ok);
    CHECK(restart.get("n1", style) == NoteResult::Ok);
    CHECK(style.alignment == PostitAlignment::Left && style.strokes[0][1].x == 1023 && style.strokes[1][0].y == 0);
    CHECK(restart.put("n1", PostitAlignment::Right, {}, 1, &style) == NoteResult::Ok);
    CHECK(style.version == 2 && style.strokes.empty() && style.alignment == PostitAlignment::Right);
    CHECK(restart.erase("n1", 1) == NoteResult::Conflict);
    CHECK(restart.erase("n1", 0) == NoteResult::InvalidArgument);
    CHECK(restart.erase("n1", 2) == NoteResult::Ok);
    CHECK(restart.eraseForNote("n1") == NoteResult::Ok);
    CHECK(store.recover() == NoteResult::Ok && store.get("n1", style) == NoteResult::NotFound);
    CHECK(store.put("n2", PostitAlignment::Center, {}, 0) == NoteResult::Ok);
    CHECK(store.eraseForNote("n2") == NoteResult::Ok);
    CHECK(store.recover() == NoteResult::Ok && store.get("n2", style) == NoteResult::NotFound);
}
void bounds() {
    MemoryFiles files; PostitStore store(files); CHECK(store.recover() == NoteResult::Ok);
    PostitStyle saved; CHECK(store.put("n1", PostitAlignment::Center, {}, 0, &saved) == NoteResult::Ok);
    const auto original = files.disk;
    for (const auto& id : {"", "../n1", "a/b", "n.1"})
        CHECK(store.put(id, PostitAlignment::Center, {}, 0) == NoteResult::InvalidArgument);
    CHECK(store.put(std::string(33,'a'), PostitAlignment::Center, {}, 0) == NoteResult::InvalidArgument);
    CHECK(store.put("n1", static_cast<PostitAlignment>(99), {}, 1) == NoteResult::InvalidArgument);
    CHECK(store.put("n1", PostitAlignment::Center, {{}}, 1) == NoteResult::InvalidArgument);
    CHECK(store.put("n1", PostitAlignment::Center, {{{1024,0}}}, 1) == NoteResult::InvalidArgument);
    CHECK(store.put("n1", PostitAlignment::Center, {{{0,65535}}}, 1) == NoteResult::InvalidArgument);
    CHECK(store.put("n1", PostitAlignment::Center, std::vector<PostitStroke>(33, {{0,0}}), 1) == NoteResult::InvalidArgument);
    CHECK(store.put("n1", PostitAlignment::Center, {PostitStroke(401, {0,0})}, 1) == NoteResult::InvalidArgument);
    CHECK(store.put("n1", PostitAlignment::Center, {PostitStroke(300, {0,0}), PostitStroke(101, {0,0})}, 1) == NoteResult::InvalidArgument);
    CHECK(store.put("n1", PostitAlignment::Center, {}, NoteStore::max_version+1) == NoteResult::InvalidArgument);
    CHECK(files.disk == original && saved.version == 1);
    auto strokes = std::vector<PostitStroke>(32, {{1023,1023}});
    strokes[0] = PostitStroke(369, {1023,1023}); // 400 total, 32 strokes.
    CHECK(store.put("n1", PostitAlignment::Center, strokes, 1, &saved) == NoteResult::Ok);
    NoteStore raw(files); CHECK(raw.recover() == NoteResult::Ok);
    CHECK(raw.notes().size() == 1 && raw.notes()[0].text.size() < 4096);
    CHECK(raw.notes()[0].title == "n1" && raw.notes()[0].audio_id.empty());
    for (unsigned char c : raw.notes()[0].text) CHECK(c < 128);
    CHECK(store.recover() == NoteResult::Ok && store.get("n1", saved) == NoteResult::Ok);
    CHECK(saved.strokes.size() == 32 && saved.strokes[0].size() == 369);
}
void corrupt() {
    for (const std::string text : {"MCPOSTIT2\nC\n", "MCPOSTIT1\nQ\n", "MCPOSTIT1\nC\n\n", "MCPOSTIT1\nC\n400000\n", "MCPOSTIT1\nC\n00000a\n", "MCPOSTIT1\nC\n000000", "MCPOSTIT1\nC\n0\n"}) {
        MemoryFiles files; NoteStore raw(files); CHECK(raw.recover() == NoteResult::Ok);
        CHECK(raw.create({"n1", text, false, ""}) == NoteResult::Ok);
        const auto snapshot = files.disk;
        PostitStore store(files); PostitStyle out;
        CHECK(store.recover() == NoteResult::Corrupt && !store.ready());
        CHECK(store.get("n1", out) == NoteResult::NotReady);
        CHECK(store.put("n1", PostitAlignment::Center, {}, 0) == NoteResult::NotReady);
        CHECK(files.disk == snapshot);
    }
    for (int bad = 0; bad < 4; ++bad) {
        MemoryFiles files; NoteStore raw(files); CHECK(raw.recover() == NoteResult::Ok);
        CHECK(raw.create({bad == 0 ? "bad.id" : "n1", "MCPOSTIT1\nC\n", bad == 1, bad == 2 ? "audio" : ""}) == NoteResult::Ok);
        if (bad == 3) CHECK(raw.create({"n1", "MCPOSTIT1\nR\n", false, ""}) == NoteResult::Ok);
        PostitStore store(files); CHECK(store.recover() == NoteResult::Corrupt && !store.ready());
    }
    MemoryFiles files; PostitStore store(files); CHECK(store.recover() == NoteResult::Ok);
    CHECK(store.put("n1", PostitAlignment::Left, {}, 0) == NoteResult::Ok);
    CHECK(store.put("n1", PostitAlignment::Right, {}, 1) == NoteResult::Ok);
    files.disk["notes-a.bin"][30] ^= 1; // Old committed corruption also blocks recovery.
    CHECK(store.recover() == NoteResult::Corrupt && !store.ready());
    files.read_error = true; CHECK(store.recover() == NoteResult::IoError);
}
void uncertainWrites() {
    for (bool committed : {false, true}) {
        MemoryFiles files; PostitStore store(files); PostitStyle saved;
        CHECK(store.recover() == NoteResult::Ok);
        CHECK(store.put("n1", PostitAlignment::Left, {}, 0, &saved) == NoteResult::Ok);
        files.write_error = !committed; files.rename_after_error = committed;
        CHECK(store.put("n1", PostitAlignment::Right, {{{0,0}}}, 1, &saved) == NoteResult::IoError);
        CHECK(saved.version == 1 && !store.ready());
        CHECK(store.eraseForNote("n1") == NoteResult::NotReady);
        files.write_error = files.rename_after_error = false;
        PostitStore restart(files); CHECK(restart.recover() == NoteResult::Ok);
        CHECK(restart.get("n1", saved) == NoteResult::Ok);
        CHECK(saved.version == (committed ? 2U : 1U));
        CHECK(saved.alignment == (committed ? PostitAlignment::Right : PostitAlignment::Left));
    }
}
int main() { lifecycle(); bounds(); corrupt(); uncertainWrites(); std::cout << "postit store tests passed\n"; }
