#pragma once

#include "note_store.hpp"

namespace compagnon {

// ESP-IDF VFS/POSIX adapter for an ALREADY mounted FAT directory. Does not
// create/mount/format storage. Supply an absolute, pre-created private directory
// with no '.', '..' or symlink components. The application must serialize ALL
// operations and prohibit external writers, directory replacement and unmounts
// for their duration; path checks are not a sandbox against concurrent attackers.
// FAT has no symlinks; host builds additionally reject symlink parents/files.
// File fsync is required. Rename/directory metadata and brownout durability are
// NOT guaranteed; a reported error may follow an effect. Recover before retry.
class PosixNoteFiles final : public NoteFiles {
public:
    explicit PosixNoteFiles(std::string directory);
    NoteIoResult read(const std::string& name, std::size_t max_bytes,
                      std::string& bytes) override;
    NoteIoResult writeExclusive(const std::string& name,
                                const std::string& bytes) override;
    NoteIoResult remove(const std::string& name) override;
    NoteIoResult renameAbsent(const std::string& from,
                              const std::string& to) override;
private:
    bool baseAvailable() const;
    bool pathFor(const std::string& name, std::string& path) const;
    NoteIoResult inspect(const std::string& path) const;
    std::string directory_;
};
} // namespace compagnon
