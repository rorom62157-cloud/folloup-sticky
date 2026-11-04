#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace compagnon {
enum class WavResult { Ok, InvalidArgument, InvalidState, Exists, LimitExceeded, IoError };
enum class WavState { Idle, Writing, Fault, Aborted, Finished };

// Optional narrow fault-injection seam. Null members use the system operation.
// Hooks must behave like fwrite/fflush/fsync/rename and set errno on failure.
struct WavIo {
    std::size_t (*write)(const void*, std::size_t, std::size_t, std::FILE*) = nullptr;
    int (*flush)(std::FILE*) = nullptr;
    int (*sync)(int) = nullptr;
    int (*rename_file)(const char*, const char*) = nullptr;
};

// One instance owns one capture. Caller serializes operations and supplies a
// unique final absolute path in a directory exclusively managed by that caller.
// No allocation proportional to audio length; writes are at most 4096 bytes.
// PCM bytes MUST be signed 16-bit little-endian mono at 16000 Hz.
//
// A .part is created exclusively and never removed by this class. Finish syncs
// the file, closes it, checks that final is absent, then renames to an absent
// destination (compatible with FAT rename semantics). Index/consume only after
// finish() succeeds. Directory exclusivity is required between that check and
// rename; POSIX rename can overwrite if an external directory mutator races it.
// On ESP_PLATFORM the existence check uses stat (FAT has no symlinks); elsewhere
// lstat also rejects dangling symlinks. No target cross-compilation is implied.
// Parent-directory fsync is NOT performed: successful publication is not a
// guarantee that the rename survives power loss. VFS fsync support is required;
// unsupported sync fails finish rather than reporting a durable save.
class WavWriter {
public:
    static constexpr std::size_t max_samples = 3840000;
    static constexpr std::size_t max_data_bytes = max_samples * 2;
    explicit WavWriter(WavIo io = {});
    ~WavWriter();
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;
    WavWriter(WavWriter&&) = delete;
    WavWriter& operator=(WavWriter&&) = delete;
    WavResult begin(const std::string& final_absolute_path);
    // Rejects null, empty, odd-byte, or over-limit input before writing anything.
    WavResult append(const void* pcm_little_endian, std::size_t bytes);
    WavResult finish();
    // Best-effort flush/sync/close. Preserves .part, including after any fault.
    // An aborted partial file may have an unfinished header; recovery is external.
    WavResult abort();
    WavState state() const { return state_; }
    std::size_t dataBytesWritten() const { return bytes_; }
    const std::string& partialPath() const { return part_path_; }
    int systemError() const { return error_; }
private:
    WavResult fail(int error);
    bool writeHeader(std::uint32_t bytes);
    WavIo io_;
    std::FILE* file_ = nullptr;
    WavState state_ = WavState::Idle;
    std::string final_path_, part_path_;
    std::size_t bytes_ = 0;
    int error_ = 0;
};
} // namespace compagnon
