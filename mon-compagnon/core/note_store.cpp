#include "note_store.hpp"

#include <algorithm>
#include <utility>

namespace compagnon {
namespace {
const std::string slots[] = {"notes-a.bin", "notes-b.bin"};
const std::string partial = "notes-next.part";

bool token(const std::string& value, std::size_t limit) {
    if (value.empty() || value.size() > limit) return false;
    for (unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
// Reject malformed UTF-8, NUL and non-text ASCII controls. Lengths are bytes.
bool text(const std::string& value, std::size_t limit) {
    if (value.size() > limit) return false;
    for (std::size_t i = 0; i < value.size();) {
        const auto c = static_cast<unsigned char>(value[i++]);
        if (c < 0x80) {
            if ((c < 32 && c != 9 && c != 10 && c != 13) || c == 127) return false;
            continue;
        }
        unsigned count = 0;
        std::uint32_t cp = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { count = 1; cp = c & 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { count = 2; cp = c & 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { count = 3; cp = c & 7; minimum = 0x10000; }
        else return false;
        if (value.size() - i < count) return false;
        while (count--) {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (next & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
bool valid(const NoteDraft& n) {
    return text(n.title, NoteStore::max_title_bytes) &&
           text(n.text, NoteStore::max_text_bytes) &&
           (n.audio_id.empty() || token(n.audio_id, NoteStore::max_audio_id_bytes));
}
void number(std::string& out, std::uint64_t n, unsigned bytes) {
    while (bytes--) { out.push_back(static_cast<char>(n & 255)); n >>= 8; }
}
void field(std::string& out, const std::string& value) {
    number(out, value.size(), 4); out += value;
}
std::uint32_t crc(const std::string& data, std::size_t size) {
    std::uint32_t result = 0xffffffff;
    for (std::size_t i = 0; i < size; ++i) {
        result ^= static_cast<unsigned char>(data[i]);
        for (int b = 0; b < 8; ++b)
            result = (result >> 1) ^ ((result & 1) ? 0xedb88320U : 0U);
    }
    return ~result;
}
std::string encode(const std::vector<Note>& notes, std::uint64_t generation) {
    std::string out = "MCNOTE01";
    number(out, generation, 8); number(out, notes.size(), 4);
    for (const auto& n : notes) {
        field(out, n.id); field(out, n.title); field(out, n.text);
        number(out, n.pinned ? 1 : 0, 1); number(out, n.version, 8);
        field(out, n.audio_id);
    }
    number(out, crc(out, out.size()), 4);
    return out;
}
struct Reader {
    const std::string& bytes;
    std::size_t pos = 8;
    bool ok = true;
    std::uint64_t number(unsigned count) {
        if (!ok || bytes.size() - pos < count) { ok = false; return 0; }
        std::uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= std::uint64_t(static_cast<unsigned char>(bytes[pos++])) << (8 * i);
        return value;
    }
    std::string field(std::size_t max) {
        const auto size = number(4);
        if (!ok || size > max || size > bytes.size() - pos) { ok = false; return {}; }
        auto value = bytes.substr(pos, static_cast<std::size_t>(size));
        pos += static_cast<std::size_t>(size); return value;
    }
};
bool decode(const std::string& bytes, std::vector<Note>& notes, std::uint64_t& gen) {
    if (bytes.size() < 24 || bytes.size() > NoteStore::max_snapshot_bytes ||
        bytes.compare(0, 8, "MCNOTE01") != 0) return false;
    Reader r{bytes};
    gen = r.number(8);
    const auto count = r.number(4);
    if (gen == 0 || gen > NoteStore::max_version || count > NoteStore::max_notes) return false;
    for (std::uint64_t i = 0; i < count; ++i) {
        Note n;
        n.id = r.field(32); n.title = r.field(NoteStore::max_title_bytes);
        n.text = r.field(NoteStore::max_text_bytes);
        const auto pin = r.number(1); n.pinned = pin == 1;
        n.version = r.number(8); n.audio_id = r.field(NoteStore::max_audio_id_bytes);
        if (!r.ok || !token(n.id, 32) || pin > 1 || n.version == 0 || n.version > gen ||
            !valid({n.title, n.text, n.pinned, n.audio_id}) ||
            std::any_of(notes.begin(), notes.end(), [&](const Note& x) { return x.id == n.id; }))
            return false;
        notes.push_back(std::move(n));
    }
    if (!r.ok || r.pos != bytes.size() - 4) return false;
    return r.number(4) == crc(bytes, bytes.size() - 4);
}
bool removed(NoteIoResult result) {
    return result == NoteIoResult::Ok || result == NoteIoResult::Missing;
}
} // namespace

NoteResult NoteStore::recover() {
    ready_ = false; notes_.clear(); generation_ = 0; slot_ = -1;
    std::string bytes[2];
    std::vector<Note> parsed[2];
    std::uint64_t gen[2] = {0, 0};
    bool good[2] = {false, false};
    for (int i = 0; i < 2; ++i) {
        const auto result = files_.read(slots[i], max_snapshot_bytes, bytes[i]);
        if (result == NoteIoResult::Missing) continue;
        if (result != NoteIoResult::Ok) return NoteResult::IoError;
        good[i] = decode(bytes[i], parsed[i], gen[i]);
        // Falling back to an earlier generation can reuse IDs and revisions,
        // making a stale request target unrelated replacement content (ABA).
        // Preserve all files and require explicit salvage outside this store.
        if (!good[i]) return NoteResult::Corrupt;
    }
    if (good[0] && good[1] && gen[0] == gen[1] && bytes[0] != bytes[1])
        return NoteResult::Corrupt;
    if (good[0] || good[1]) {
        slot_ = good[1] && (!good[0] || gen[1] > gen[0]) ? 1 : 0;
        generation_ = gen[slot_]; notes_ = std::move(parsed[slot_]);
    }
    ready_ = true;
    return NoteResult::Ok;
}
const Note* NoteStore::find(const std::string& id) const {
    if (!ready_) return nullptr;
    const auto it = std::find_if(notes_.begin(), notes_.end(), [&](const Note& n) { return n.id == id; });
    return it == notes_.end() ? nullptr : &*it;
}
NoteResult NoteStore::commit(std::vector<Note> next) {
    if (generation_ == max_version) return NoteResult::LimitExceeded;
    const auto bytes = encode(next, generation_ + 1);
    if (bytes.size() > max_snapshot_bytes) return NoteResult::LimitExceeded;
    const int target = slot_ == 0 ? 1 : 0;
    // The currently selected committed slot is never removed.
    if (!removed(files_.remove(partial)) || !removed(files_.remove(slots[target])) ||
        files_.writeExclusive(partial, bytes) != NoteIoResult::Ok ||
        files_.renameAbsent(partial, slots[target]) != NoteIoResult::Ok) {
        ready_ = false; notes_.clear(); return NoteResult::IoError;
    }
    notes_ = std::move(next); ++generation_; slot_ = target;
    return NoteResult::Ok;
}
NoteResult NoteStore::create(const NoteDraft& draft, Note* saved) {
    if (!ready_) return NoteResult::NotReady;
    if (!valid(draft)) return NoteResult::InvalidArgument;
    if (notes_.size() >= max_notes || generation_ == max_version) return NoteResult::LimitExceeded;
    Note n{"n" + std::to_string(generation_ + 1), draft.title, draft.text,
           draft.pinned, 1, draft.audio_id};
    auto next = notes_; next.push_back(n);
    const auto result = commit(std::move(next));
    if (result == NoteResult::Ok && saved) *saved = std::move(n);
    return result;
}
NoteResult NoteStore::update(const std::string& id, const NoteDraft& draft,
                             std::uint64_t expected_version, Note* saved) {
    if (!ready_) return NoteResult::NotReady;
    if (!token(id, 32) || !valid(draft)) return NoteResult::InvalidArgument;
    const auto* current = find(id);
    if (!current) return NoteResult::NotFound;
    if (current->version != expected_version) return NoteResult::Conflict;
    if (current->version == max_version) return NoteResult::LimitExceeded;
    Note n{id, draft.title, draft.text, draft.pinned, current->version + 1, draft.audio_id};
    auto next = notes_;
    for (auto& item : next) if (item.id == id) item = n;
    const auto result = commit(std::move(next));
    if (result == NoteResult::Ok && saved) *saved = std::move(n);
    return result;
}
NoteResult NoteStore::setPinned(const std::string& id, bool pinned,
                                std::uint64_t expected_version, Note* saved) {
    if (!ready_) return NoteResult::NotReady;
    if (!token(id, 32)) return NoteResult::InvalidArgument;
    const auto* n = find(id);
    if (!n) return NoteResult::NotFound;
    return update(id, {n->title, n->text, pinned, n->audio_id}, expected_version, saved);
}
NoteResult NoteStore::erase(const std::string& id, std::uint64_t expected_version) {
    if (!ready_) return NoteResult::NotReady;
    if (!token(id, 32)) return NoteResult::InvalidArgument;
    const auto* n = find(id);
    if (!n) return NoteResult::NotFound;
    if (n->version != expected_version) return NoteResult::Conflict;
    auto next = notes_;
    next.erase(std::remove_if(next.begin(), next.end(), [&](const Note& x) { return x.id == id; }), next.end());
    return commit(std::move(next));
}
} // namespace compagnon
