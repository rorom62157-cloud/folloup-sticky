#include "task_store.hpp"

namespace compagnon {
namespace {
Task task(const Note& note) {
    return {note.id, note.title, note.text, note.pinned, note.version};
}
bool whitespace(std::uint32_t cp) {
    return cp == 0x20 || (cp >= 0x09 && cp <= 0x0d) || cp == 0x85 ||
           cp == 0xa0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) ||
           cp == 0x2028 || cp == 0x2029 || cp == 0x202f || cp == 0x205f ||
           cp == 0x3000;
}
// This only detects blank titles. NoteStore remains the strict UTF-8 validator.
bool nonblank(const std::string& title) {
    for (std::size_t i = 0; i < title.size();) {
        const auto c = static_cast<unsigned char>(title[i++]);
        std::uint32_t cp = c;
        unsigned count = 0;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; count = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; count = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; count = 3; }
        else if (c >= 0x80) return true; // Invalid input is rejected by NoteStore.
        if (title.size() - i < count) return true;
        while (count--) {
            const auto next = static_cast<unsigned char>(title[i++]);
            if ((next & 0xc0) != 0x80) return true;
            cp = (cp << 6) | (next & 63);
        }
        if (!whitespace(cp)) return true;
    }
    return false;
}
NoteResult output(NoteResult result, const Note& note, Task* saved) {
    if (result == NoteResult::Ok && saved) *saved = task(note);
    return result;
}
} // namespace

NoteResult TaskStore::recover() {
    valid_ = false;
    const auto result = store_.recover();
    if (result != NoteResult::Ok) return result;
    for (const auto& note : store_.notes())
        if (!nonblank(note.title) || !note.audio_id.empty()) return NoteResult::Corrupt;
    valid_ = true;
    return NoteResult::Ok;
}
std::vector<Task> TaskStore::tasks() const {
    std::vector<Task> result;
    if (!ready()) return result;
    result.reserve(store_.notes().size());
    for (const auto& note : store_.notes()) result.push_back(task(note));
    return result;
}
NoteResult TaskStore::create(const std::string& title, const std::string& details,
                             Task* saved) {
    if (!ready()) return NoteResult::NotReady;
    if (!nonblank(title)) return NoteResult::InvalidArgument;
    Note note;
    const auto result = store_.create({title, details, false, ""}, &note);
    return output(result, note, saved);
}
NoteResult TaskStore::update(const std::string& id, const std::string& title,
                             const std::string& details, std::uint64_t expected_version,
                             Task* saved) {
    if (!ready()) return NoteResult::NotReady;
    if (!nonblank(title)) return NoteResult::InvalidArgument;
    const auto* current = store_.find(id);
    Note note;
    const auto result = store_.update(id, {title, details, current && current->pinned, ""},
                                      expected_version, &note);
    return output(result, note, saved);
}
NoteResult TaskStore::setDone(const std::string& id, bool done,
                              std::uint64_t expected_version, Task* saved) {
    if (!ready()) return NoteResult::NotReady;
    Note note;
    const auto result = store_.setPinned(id, done, expected_version, &note);
    return output(result, note, saved);
}
NoteResult TaskStore::erase(const std::string& id, std::uint64_t expected_version) {
    if (!ready()) return NoteResult::NotReady;
    return store_.erase(id, expected_version);
}
} // namespace compagnon
