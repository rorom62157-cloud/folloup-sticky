#pragma once

#include "note_store.hpp"

namespace compagnon {

enum class PostitAlignment { Left, Center, Right };
struct PostitPoint { std::uint16_t x = 0, y = 0; };
using PostitStroke = std::vector<PostitPoint>;
struct PostitStyle {
    std::string note_id;
    PostitAlignment alignment = PostitAlignment::Center;
    std::vector<PostitStroke> strokes;
    std::uint64_t version = 0;
};

// The supplied NoteFiles must own a separate private directory (device path:
// /sdcard/compagnon-postits), never the directory of the content NoteStore.
// Caller authorizes access, verifies the target note exists and serializes the
// entire operation with all note users. This store owns presentation only.
// After an I/O failure recovery is required; the operation may have committed.
class PostitStore {
public:
    static constexpr std::size_t max_strokes = 32;
    static constexpr std::size_t max_points = 400;
    static constexpr std::uint16_t coordinate_max = 1023;
    explicit PostitStore(NoteFiles& files) : store_(files) {}
    NoteResult recover();
    bool ready() const { return ready_ && store_.ready(); }
    NoteResult get(const std::string& note_id, PostitStyle& style) const;
    // Zero creates only an absent style; a positive version updates only an
    // existing style. saved is changed only when the write succeeds.
    NoteResult put(const std::string& note_id, PostitAlignment alignment,
                   const std::vector<PostitStroke>& strokes,
                   std::uint64_t expected_version, PostitStyle* saved = nullptr);
    NoteResult erase(const std::string& note_id, std::uint64_t expected_version);
    // Native note-deletion cleanup; absence is already clean and returns Ok.
    NoteResult eraseForNote(const std::string& note_id);
private:
    const Note* find(const std::string& note_id) const;
    NoteStore store_;
    bool ready_ = false;
};
} // namespace compagnon
