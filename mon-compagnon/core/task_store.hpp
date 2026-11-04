#pragma once

#include "note_store.hpp"

namespace compagnon {

struct Task {
    std::string id, title, details;
    bool done = false;
    std::uint64_t version = 0;
};

// Caller serializes access. Supply a dedicated NoteFiles directory, never the
// notes directory: the underlying snapshot names and IDs are shared formats.
// No scheduling, Google synchronization, audio ownership or authorization.
// An I/O error requires explicit recover(); do not automatically retry creates.
class TaskStore {
public:
    static constexpr std::size_t max_tasks = NoteStore::max_notes;
    static constexpr std::size_t max_title_bytes = NoteStore::max_title_bytes;
    static constexpr std::size_t max_details_bytes = NoteStore::max_text_bytes;
    static constexpr std::uint64_t max_version = NoteStore::max_version;
    explicit TaskStore(NoteFiles& files) : store_(files) {}
    NoteResult recover();
    bool ready() const { return valid_ && store_.ready(); }
    // Owned values, safe to retain across later mutations; empty when not ready.
    std::vector<Task> tasks() const;
    NoteResult create(const std::string& title, const std::string& details,
                      Task* saved = nullptr);
    NoteResult update(const std::string& id, const std::string& title,
                      const std::string& details, std::uint64_t expected_version,
                      Task* saved = nullptr);
    NoteResult setDone(const std::string& id, bool done,
                       std::uint64_t expected_version, Task* saved = nullptr);
    NoteResult erase(const std::string& id, std::uint64_t expected_version);
private:
    NoteStore store_;
    bool valid_ = false;
};
} // namespace compagnon
