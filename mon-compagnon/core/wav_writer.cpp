#include "wav_writer.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace compagnon {
namespace {
bool safePath(const std::string& path) {
    if (path.size() < 2 || path.size() > 4090 || path.front() != '/' ||
        path.back() == '/' || path.find('\0') != std::string::npos) return false;
    std::size_t start = 1;
    while (start < path.size()) {
        auto end = path.find('/',start);
        if (end == std::string::npos) end = path.size();
        auto component = path.substr(start,end-start);
        if (component.empty() || component == "." || component == "..") return false;
        start = end + 1;
    }
    return true;
}
void put16(unsigned char* p, std::uint16_t value) {
    p[0] = static_cast<unsigned char>(value); p[1] = static_cast<unsigned char>(value >> 8);
}
void put32(unsigned char* p, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (8*i));
}
int pathStatus(const char* path, struct stat* info) {
#ifdef ESP_PLATFORM
    return ::stat(path,info);
#else
    return ::lstat(path,info);
#endif
}
}
WavWriter::WavWriter(WavIo io) : io_(io) {
    if (!io_.write) io_.write = std::fwrite;
    if (!io_.flush) io_.flush = std::fflush;
    if (!io_.sync) io_.sync = ::fsync;
    if (!io_.rename_file) io_.rename_file = ::rename;
}
WavWriter::~WavWriter() { if (file_) (void)abort(); }
WavResult WavWriter::fail(int error) {
    error_ = error ? error : EIO; state_ = WavState::Fault; return WavResult::IoError;
}
bool WavWriter::writeHeader(std::uint32_t bytes) {
    std::array<unsigned char,44> h{};
    const char riff[] = "RIFF", wave[] = "WAVE", fmt[] = "fmt ", data[] = "data";
    std::copy_n(riff,4,h.begin()); std::copy_n(wave,4,h.begin()+8);
    std::copy_n(fmt,4,h.begin()+12); std::copy_n(data,4,h.begin()+36);
    put32(h.data()+4,36+bytes); put32(h.data()+16,16);
    put16(h.data()+20,1); put16(h.data()+22,1); put32(h.data()+24,16000);
    put32(h.data()+28,32000); put16(h.data()+32,2); put16(h.data()+34,16);
    put32(h.data()+40,bytes);
    return io_.write(h.data(),1,h.size(),file_) == h.size();
}
WavResult WavWriter::begin(const std::string& path) {
    if (state_ != WavState::Idle) return WavResult::InvalidState;
    if (!safePath(path)) return WavResult::InvalidArgument;
    struct stat info{};
    if (pathStatus(path.c_str(),&info) == 0) return WavResult::Exists;
    if (errno != ENOENT) { error_ = errno; return WavResult::IoError; }
    final_path_ = path; part_path_ = path + ".part";
    const int fd = ::open(part_path_.c_str(),O_CREAT|O_EXCL|O_RDWR,0600);
    if (fd < 0) { error_ = errno; return errno == EEXIST ? WavResult::Exists : WavResult::IoError; }
    file_ = ::fdopen(fd,"wb+");
    if (!file_) { const int error = errno; ::close(fd); return fail(error); }
    if (!writeHeader(0)) return fail(errno);
    state_ = WavState::Writing;
    return WavResult::Ok;
}
WavResult WavWriter::append(const void* pcm, std::size_t bytes) {
    if (state_ != WavState::Writing) return WavResult::InvalidState;
    if (!pcm || bytes == 0 || bytes % 2) return WavResult::InvalidArgument;
    if (bytes > max_data_bytes - bytes_) return WavResult::LimitExceeded;
    auto p = static_cast<const unsigned char*>(pcm);
    while (bytes) {
        const auto chunk = std::min<std::size_t>(bytes,4096);
        const auto written = io_.write(p,1,chunk,file_);
        bytes_ += written;
        if (written != chunk) return fail(errno);
        bytes -= chunk; p += chunk;
    }
    return WavResult::Ok;
}
WavResult WavWriter::finish() {
    if (state_ != WavState::Writing) return WavResult::InvalidState;
    if (std::fseek(file_,0,SEEK_SET) != 0 || !writeHeader(static_cast<std::uint32_t>(bytes_))) return fail(errno);
    if (io_.flush(file_) != 0 || io_.sync(::fileno(file_)) != 0) return fail(errno);
    const int closed = std::fclose(file_); file_ = nullptr;
    if (closed != 0) return fail(errno);
    // Caller owns/serializes the directory. Do not reserve the final name:
    // FAT rename rejects an existing destination, including an empty reservation.
    struct stat info{};
    if (pathStatus(final_path_.c_str(),&info) == 0) {
        error_ = EEXIST; state_ = WavState::Fault; return WavResult::Exists;
    }
    if (errno != ENOENT) return fail(errno);
    if (io_.rename_file(part_path_.c_str(),final_path_.c_str()) != 0) {
        const int error = errno;
        if (error == EEXIST) { error_ = error; state_ = WavState::Fault; return WavResult::Exists; }
        return fail(error);
    }
    state_ = WavState::Finished; return WavResult::Ok;
}
WavResult WavWriter::abort() {
    if (state_ == WavState::Idle || state_ == WavState::Finished || state_ == WavState::Aborted)
        return WavResult::InvalidState;
    int failure = 0;
    if (file_) {
        if (io_.flush(file_) != 0) failure = errno ? errno : EIO;
        if (io_.sync(::fileno(file_)) != 0 && !failure) failure = errno ? errno : EIO;
        if (std::fclose(file_) != 0 && !failure) failure = errno ? errno : EIO;
        file_ = nullptr;
    }
    state_ = WavState::Aborted;
    if (failure) { error_ = failure; return WavResult::IoError; }
    return WavResult::Ok;
}
} // namespace compagnon
