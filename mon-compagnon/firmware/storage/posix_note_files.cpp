#include "posix_note_files.hpp"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace compagnon {
namespace {
bool allowed(const std::string& name) {
    return name == "notes-a.bin" || name == "notes-b.bin" ||
           name == "notes-next.part";
}
int inspectPath(const char* path, struct stat* st) {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
    // ESP-IDF FAT VFS has no symlinks and does not expose lstat. This adapter
    // is supported only for FAT there, not a symlink-capable VFS backend.
    return ::stat(path, st);
#else
    return ::lstat(path, st);
#endif
}
int noFollow() {
#ifdef O_NOFOLLOW
    return O_NOFOLLOW;
#else
    return 0; // FAT has no links; lstat checks still apply on other hosts.
#endif
}
} // namespace

PosixNoteFiles::PosixNoteFiles(std::string directory)
    : directory_(std::move(directory)) {}

bool PosixNoteFiles::baseAvailable() const {
    if (directory_.size() < 2 || directory_[0] != '/' ||
        directory_.back() == '/' || directory_.find('\0') != std::string::npos)
        return false;
    // Validate each parent separately: lstat(final) alone follows parent links.
    for (std::size_t start = 1; start < directory_.size();) {
        const auto slash = directory_.find('/', start);
        const auto end = slash == std::string::npos ? directory_.size() : slash;
        const auto part = directory_.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        struct stat st {};
        if (inspectPath(directory_.substr(0, end).c_str(), &st) != 0 ||
            !S_ISDIR(st.st_mode)) return false;
        start = end + 1;
    }
    return true;
}

bool PosixNoteFiles::pathFor(const std::string& name, std::string& path) const {
    if (!allowed(name) || !baseAvailable()) return false;
    path = directory_ + "/" + name;
    return true;
}

NoteIoResult PosixNoteFiles::inspect(const std::string& path) const {
    struct stat st {};
    if (inspectPath(path.c_str(), &st) == 0)
        return S_ISREG(st.st_mode) ? NoteIoResult::Exists : NoteIoResult::Error;
    const int error = errno;
    // ENOENT is absence only while the enclosing storage is available.
    if (error == ENOENT && baseAvailable()) return NoteIoResult::Missing;
    return NoteIoResult::Error;
}

NoteIoResult PosixNoteFiles::read(const std::string& name, std::size_t max_bytes,
                                 std::string& bytes) {
    bytes.clear();
    std::string path;
    if (!pathFor(name, path)) return NoteIoResult::Error;
    const auto state = inspect(path);
    if (state != NoteIoResult::Exists) return state;
    const int fd = ::open(path.c_str(), O_RDONLY | noFollow());
    if (fd < 0) return NoteIoResult::Error;
    struct stat st {};
    bool ok = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 &&
        static_cast<std::uintmax_t>(st.st_size) <= max_bytes &&
        static_cast<std::uintmax_t>(st.st_size) <= NoteStore::max_snapshot_bytes;
    std::string content;
    if (ok) {
        content.resize(static_cast<std::size_t>(st.st_size));
        std::size_t done = 0;
        while (done < content.size()) {
            const auto count = ::read(fd, &content[done], content.size() - done);
            if (count <= 0) { ok = false; break; }
            done += static_cast<std::size_t>(count);
        }
        char extra;
        if (ok && ::read(fd, &extra, 1) != 0) ok = false;
    }
    if (::close(fd) != 0) ok = false;
    if (!ok) return NoteIoResult::Error;
    bytes.swap(content);
    return NoteIoResult::Ok;
}

NoteIoResult PosixNoteFiles::writeExclusive(const std::string& name,
                                           const std::string& bytes) {
    std::string path;
    if (bytes.size() > NoteStore::max_snapshot_bytes || !pathFor(name, path))
        return NoteIoResult::Error;
    const auto state = inspect(path);
    if (state != NoteIoResult::Missing) return state;
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | noFollow(), 0600);
    if (fd < 0) {
        if (errno == EEXIST && inspect(path) == NoteIoResult::Exists)
            return NoteIoResult::Exists;
        return NoteIoResult::Error;
    }
    FILE* file = ::fdopen(fd, "wb");
    if (!file) { ::close(fd); return NoteIoResult::Error; }
    bool ok = ::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    // Evaluate every required operation, including close after earlier failure.
    if (::fflush(file) != 0) ok = false;
    if (::fsync(fd) != 0) ok = false; // ENOSYS/ENOTSUP is an error, never success.
    if (::fclose(file) != 0) ok = false;
    return ok ? NoteIoResult::Ok : NoteIoResult::Error;
}

NoteIoResult PosixNoteFiles::remove(const std::string& name) {
    std::string path;
    if (!pathFor(name, path)) return NoteIoResult::Error;
    const auto state = inspect(path);
    if (state != NoteIoResult::Exists) return state;
    return ::unlink(path.c_str()) == 0 ? NoteIoResult::Ok : NoteIoResult::Error;
}

NoteIoResult PosixNoteFiles::renameAbsent(const std::string& from,
                                         const std::string& to) {
    std::string source, target;
    if (!pathFor(from, source) || !pathFor(to, target)) return NoteIoResult::Error;
    const auto dest = inspect(target);
    if (dest != NoteIoResult::Missing) return dest;
    const auto src = inspect(source);
    if (src != NoteIoResult::Exists) return src;
    // POSIX rename can replace: absence is safe ONLY under the documented
    // single-owner/no-external-writers contract (also needed on FAT VFS).
    return ::rename(source.c_str(), target.c_str()) == 0
        ? NoteIoResult::Ok : NoteIoResult::Error;
}
} // namespace compagnon
