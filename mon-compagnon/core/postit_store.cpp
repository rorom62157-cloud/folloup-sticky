#include "postit_store.hpp"

#include <algorithm>
#include <utility>

namespace compagnon {
namespace {
bool validId(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
char alignmentCode(PostitAlignment alignment) {
    switch (alignment) {
    case PostitAlignment::Left: return 'L';
    case PostitAlignment::Center: return 'C';
    case PostitAlignment::Right: return 'R';
    }
    return 0;
}
bool valid(PostitAlignment alignment, const std::vector<PostitStroke>& strokes) {
    if (!alignmentCode(alignment) || strokes.size() > PostitStore::max_strokes) return false;
    std::size_t total = 0;
    for (const auto& stroke : strokes) {
        if (stroke.empty() || stroke.size() > PostitStore::max_points - total) return false;
        total += stroke.size();
        for (const auto& point : stroke)
            if (point.x > PostitStore::coordinate_max || point.y > PostitStore::coordinate_max) return false;
    }
    return true;
}
void coordinate(std::string& text, std::uint16_t value) {
    constexpr char digits[] = "0123456789ABCDEF";
    for (int shift : {8, 4, 0}) text += digits[(value >> shift) & 15];
}
std::string encode(PostitAlignment alignment, const std::vector<PostitStroke>& strokes) {
    std::string text = "MCPOSTIT1\n";
    text += alignmentCode(alignment); text += '\n';
    for (const auto& stroke : strokes) {
        for (const auto& point : stroke) { coordinate(text, point.x); coordinate(text, point.y); }
        text += '\n';
    }
    return text;
}
bool decode(const Note& note, PostitStyle& out) {
    const auto& text = note.text;
    if (!validId(note.title) || note.pinned || !note.audio_id.empty() ||
        text.size() >= NoteStore::max_text_bytes || text.size() < 12 ||
        text.compare(0, 10, "MCPOSTIT1\n") != 0 || text[11] != '\n') return false;
    PostitStyle parsed;
    parsed.note_id = note.title; parsed.version = note.version;
    switch (text[10]) {
    case 'L': parsed.alignment = PostitAlignment::Left; break;
    case 'C': parsed.alignment = PostitAlignment::Center; break;
    case 'R': parsed.alignment = PostitAlignment::Right; break;
    default: return false;
    }
    std::size_t pos = 12, total = 0;
    while (pos < text.size()) {
        if (parsed.strokes.size() >= PostitStore::max_strokes) return false;
        const auto end = text.find('\n', pos);
        if (end == std::string::npos || end == pos || (end - pos) % 6 != 0) return false;
        const auto points = (end - pos) / 6;
        if (points > PostitStore::max_points - total) return false;
        total += points;
        PostitStroke stroke;
        while (pos < end) {
            std::uint16_t values[2] = {0, 0};
            for (auto& value : values) {
                for (unsigned digit = 0; digit < 3; ++digit) {
                    const char c = text[pos++];
                    const int nibble = c >= '0' && c <= '9' ? c - '0' :
                                       c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                    if (nibble < 0) return false;
                    value = static_cast<std::uint16_t>((value << 4) | nibble);
                }
                if (value > PostitStore::coordinate_max) return false;
            }
            stroke.push_back({values[0], values[1]});
        }
        parsed.strokes.push_back(std::move(stroke)); pos = end + 1;
    }
    out = std::move(parsed);
    return true;
}
} // namespace

NoteResult PostitStore::recover() {
    ready_ = false;
    const auto result = store_.recover();
    if (result != NoteResult::Ok) return result;
    std::vector<std::string> ids;
    for (const auto& note : store_.notes()) {
        PostitStyle parsed;
        if (!decode(note, parsed) || std::find(ids.begin(), ids.end(), parsed.note_id) != ids.end())
            return NoteResult::Corrupt;
        ids.push_back(parsed.note_id);
    }
    ready_ = true;
    return NoteResult::Ok;
}
const Note* PostitStore::find(const std::string& id) const {
    for (const auto& note : store_.notes()) if (note.title == id) return &note;
    return nullptr;
}
NoteResult PostitStore::get(const std::string& id, PostitStyle& style) const {
    if (!ready()) return NoteResult::NotReady;
    if (!validId(id)) return NoteResult::InvalidArgument;
    const auto* note = find(id);
    if (!note) return NoteResult::NotFound;
    return decode(*note, style) ? NoteResult::Ok : NoteResult::Corrupt;
}
NoteResult PostitStore::put(const std::string& id, PostitAlignment alignment,
                             const std::vector<PostitStroke>& strokes,
                             std::uint64_t expected, PostitStyle* saved) {
    if (!ready()) return NoteResult::NotReady;
    if (!validId(id) || !valid(alignment, strokes) || expected > NoteStore::max_version)
        return NoteResult::InvalidArgument;
    const auto* current = find(id);
    if (!current && expected != 0) return NoteResult::NotFound;
    if (current && (expected == 0 || expected != current->version)) return NoteResult::Conflict;
    const NoteDraft draft{id, encode(alignment, strokes), false, ""};
    Note written;
    const auto result = current ? store_.update(current->id, draft, expected, &written) :
                                  store_.create(draft, &written);
    if (result == NoteResult::Ok && saved) decode(written, *saved);
    return result;
}
NoteResult PostitStore::erase(const std::string& id, std::uint64_t expected) {
    if (!ready()) return NoteResult::NotReady;
    if (!validId(id) || expected == 0 || expected > NoteStore::max_version) return NoteResult::InvalidArgument;
    const auto* current = find(id);
    return current ? store_.erase(current->id, expected) : NoteResult::NotFound;
}
NoteResult PostitStore::eraseForNote(const std::string& id) {
    if (!ready()) return NoteResult::NotReady;
    if (!validId(id)) return NoteResult::InvalidArgument;
    const auto* current = find(id);
    return current ? store_.erase(current->id, current->version) : NoteResult::Ok;
}
} // namespace compagnon
