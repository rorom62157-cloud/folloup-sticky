#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace compagnon {

enum class NoteIoResult { Ok, Missing, Exists, Error };

// Adapter owns a private directory and accepts ONLY the fixed basenames below.
// No symlinks, concurrent writers or replacement renames. read() must reject
// oversized files before allocating; writeExclusive() includes flush/fsync/close.
class NoteFiles {
public:
    virtual ~NoteFiles() = default;
    virtual NoteIoResult read(const std::string& name, std::size_t max_bytes,
                              std::string& bytes) = 0;
    virtual NoteIoResult writeExclusive(const std::string& name,
                                        const std::string& bytes) = 0;
    virtual NoteIoResult remove(const std::string& name) = 0;
    virtual NoteIoResult renameAbsent(const std::string& from,
                                      const std::string& to) = 0;
};

struct Note {
    std::string id, title, text;
    bool pinned = false;
    std::uint64_t version = 0;
    std::string audio_id; // Empty means absent; never a filesystem path.
};
struct NoteDraft {
    std::string title, text;
    bool pinned = false;
    std::string audio_id;
};
enum class NoteResult {
    Ok, NotReady, InvalidArgument, LimitExceeded, NotFound, Conflict,
    IoError, Corrupt
};

// Caller serializes access. No HTTP authorization or audio ownership implied.
// On an I/O error recover() is required; the last operation can have committed
// despite a reported error. Do not automatically retry a create after that error.
class NoteStore {
public:
    static constexpr std::size_t max_notes = 32;
    static constexpr std::size_t max_title_bytes = 160;
    static constexpr std::size_t max_text_bytes = 4096;
    static constexpr std::size_t max_audio_id_bytes = 64;
    static constexpr std::size_t max_snapshot_bytes = 150000;
    static constexpr std::uint64_t max_version = 9007199254740991ULL;
    explicit NoteStore(NoteFiles& files) : files_(files) {}
    // Any existing corrupt committed slot fails closed; salvage is external.
    NoteResult recover();
    bool ready() const { return ready_; }
    // Results are exposed only after successful recovery; invalidated on mutation.
    const std::vector<Note>& notes() const { return notes_; }
    const Note* find(const std::string& id) const;
    NoteResult create(const NoteDraft& draft, Note* saved = nullptr);
    NoteResult update(const std::string& id, const NoteDraft& draft,
                      std::uint64_t expected_version, Note* saved = nullptr);
    NoteResult setPinned(const std::string& id, bool pinned,
                         std::uint64_t expected_version, Note* saved = nullptr);
    NoteResult erase(const std::string& id, std::uint64_t expected_version);
private:
    NoteResult commit(std::vector<Note> next);
    NoteFiles& files_;
    std::vector<Note> notes_;
    std::uint64_t generation_ = 0;
    int slot_ = -1;
    bool ready_ = false;
};
} // namespace compagnon
