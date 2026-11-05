#include "wav_writer.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace compagnon;
namespace {
bool exists(const std::string& path) { struct stat s{}; return ::stat(path.c_str(),&s) == 0; }
std::vector<unsigned char> read(const std::string& path) {
    auto f = std::fopen(path.c_str(),"rb"); assert(f);
    assert(std::fseek(f,0,SEEK_END) == 0); const auto length = std::ftell(f); assert(length >= 0);
    assert(std::fseek(f,0,SEEK_SET) == 0);
    std::vector<unsigned char> data(static_cast<std::size_t>(length));
    assert(std::fread(data.data(),1,data.size(),f) == data.size()); assert(std::fclose(f) == 0);
    return data;
}
void create(const std::string& path) {
    auto f = std::fopen(path.c_str(),"wb"); assert(f);
    assert(std::fwrite("preserve",1,8,f) == 8); assert(std::fclose(f) == 0);
}
std::uint32_t u32(const std::vector<unsigned char>& v, std::size_t p) {
    return std::uint32_t(v[p]) | (std::uint32_t(v[p+1])<<8) | (std::uint32_t(v[p+2])<<16) | (std::uint32_t(v[p+3])<<24);
}
int writes = 0;
std::size_t shortWrite(const void* p, std::size_t size, std::size_t count, std::FILE* f) {
    if (++writes == 2) { errno = ENOSPC; return std::fwrite(p,size,count/2,f); }
    return std::fwrite(p,size,count,f);
}
int failSync(int) { errno = EIO; return -1; }
int fatRenameCalls = 0;
int fatRename(const char* from, const char* to) {
    ++fatRenameCalls;
    // Model f_rename: unlike POSIX rename, an existing destination is refused.
    if (exists(to)) { errno = EEXIST; return -1; }
    return ::rename(from,to);
}
int failRename(const char*, const char*) { errno = EIO; return -1; }
int collisionAtFatRename(const char* from, const char* to) {
    create(to);
    return fatRename(from,to);
}
}
int main() {
    char temp[] = "/tmp/compagnon-wav-XXXXXX";
    const char* dir = ::mkdtemp(temp); assert(dir);
    const std::string root(dir);
    const std::array<unsigned char,8> pcm{0,0,255,127,0,128,255,255};
    {
        WavWriter w({nullptr,nullptr,nullptr,fatRename});
        assert(w.begin("relative.wav") == WavResult::InvalidArgument);
        assert(w.begin(root+"/../bad.wav") == WavResult::InvalidArgument);
        assert(w.append(pcm.data(),pcm.size()) == WavResult::InvalidState);
        const auto path = root+"/valid.wav";
        assert(w.begin(path) == WavResult::Ok);
        assert(w.begin(root+"/other.wav") == WavResult::InvalidState);
        assert(!exists(path) && exists(path+".part"));
        assert(w.append(nullptr,2) == WavResult::InvalidArgument);
        assert(w.append(pcm.data(),0) == WavResult::InvalidArgument);
        assert(w.append(pcm.data(),3) == WavResult::InvalidArgument);
        assert(w.append(pcm.data(),4) == WavResult::Ok);
        assert(w.append(pcm.data()+4,4) == WavResult::Ok);
        assert(w.finish() == WavResult::Ok && w.state() == WavState::Finished);
        assert(fatRenameCalls == 1);
        assert(w.append(pcm.data(),2) == WavResult::InvalidState);
        assert(w.finish() == WavResult::InvalidState && w.abort() == WavResult::InvalidState);
        assert(!exists(path+".part"));
        auto data = read(path); assert(data.size() == 52);
        assert(std::memcmp(data.data(),"RIFF",4) == 0 && u32(data,4) == 44);
        assert(std::memcmp(data.data()+8,"WAVEfmt ",8) == 0 && u32(data,16) == 16);
        assert(data[20] == 1 && data[21] == 0 && data[22] == 1 && data[23] == 0);
        assert(u32(data,24) == 16000 && u32(data,28) == 32000);
        assert(data[32] == 2 && data[33] == 0 && data[34] == 16 && data[35] == 0);
        assert(std::memcmp(data.data()+36,"data",4) == 0 && u32(data,40) == 8);
        assert(std::memcmp(data.data()+44,pcm.data(),pcm.size()) == 0);
    }
    {
        WavWriter w; const auto path = root+"/max.wav"; assert(w.begin(path) == WavResult::Ok);
        std::array<unsigned char,4096> block{};
        std::size_t remaining = WavWriter::max_data_bytes-2;
        while (remaining) { const auto bytes = remaining < block.size() ? remaining : block.size(); assert(w.append(block.data(),bytes) == WavResult::Ok); remaining -= bytes; }
        // Reject the whole request before writing, even when part would fit.
        assert(w.append(block.data(),4) == WavResult::LimitExceeded);
        assert(w.dataBytesWritten() == WavWriter::max_data_bytes-2);
        assert(w.append(block.data(),2) == WavResult::Ok);
        assert(w.append(block.data(),2) == WavResult::LimitExceeded);
        assert(w.append(block.data(),std::numeric_limits<std::size_t>::max()-1) == WavResult::LimitExceeded);
        assert(w.finish() == WavResult::Ok);
        auto data = read(path); assert(data.size() == WavWriter::max_data_bytes+44);
        assert(u32(data,40) == WavWriter::max_data_bytes);
    }
    {
        const auto path = root+"/existing.wav"; create(path); const auto original = read(path);
        WavWriter w; assert(w.begin(path) == WavResult::Exists);
        assert(read(path) == original && !exists(path+".part"));
        const auto partial = root+"/partial.wav"; create(partial+".part");
        assert(w.begin(partial) == WavResult::Exists);
        assert(read(partial+".part") == original && !exists(partial));
    }
    {
        const auto path = root+"/collision.wav"; WavWriter w;
        assert(w.begin(path) == WavResult::Ok && w.append(pcm.data(),8) == WavResult::Ok);
        create(path); const auto original = read(path);
        assert(w.finish() == WavResult::Exists);
        assert(read(path) == original && exists(path+".part"));
        assert(w.abort() == WavResult::Ok && read(path) == original);
    }
    {
        const auto path = root+"/aborted.wav";
        { WavWriter w; assert(w.begin(path) == WavResult::Ok); assert(w.append(pcm.data(),8) == WavResult::Ok); }
        assert(!exists(path) && read(path+".part").size() == 52);
    }
    {
        const auto path = root+"/short.wav"; WavWriter w({shortWrite,nullptr,nullptr});
        assert(w.begin(path) == WavResult::Ok);
        assert(w.append(pcm.data(),8) == WavResult::IoError && w.systemError() == ENOSPC);
        assert(w.state() == WavState::Fault && w.dataBytesWritten() == 4);
        assert(w.finish() == WavResult::InvalidState);
        assert(w.abort() == WavResult::Ok && !exists(path));
        assert(read(path+".part").size() == 48);
    }
    {
        const auto path = root+"/sync.wav"; WavWriter w({nullptr,nullptr,failSync});
        assert(w.begin(path) == WavResult::Ok && w.append(pcm.data(),8) == WavResult::Ok);
        assert(w.finish() == WavResult::IoError && w.systemError() == EIO);
        assert(!exists(path) && exists(path+".part"));
        assert(w.abort() == WavResult::IoError);
    }
    {
        const auto path = root+"/rename.wav";
        WavWriter w({nullptr,nullptr,nullptr,failRename});
        assert(w.begin(path) == WavResult::Ok && w.append(pcm.data(),8) == WavResult::Ok);
        assert(w.finish() == WavResult::IoError && w.systemError() == EIO);
        assert(!exists(path) && read(path+".part").size() == 52);
        assert(w.abort() == WavResult::Ok);
    }
    {
        const auto path = root+"/fat-collision.wav";
        WavWriter w({nullptr,nullptr,nullptr,collisionAtFatRename});
        assert(w.begin(path) == WavResult::Ok && w.append(pcm.data(),8) == WavResult::Ok);
        assert(w.finish() == WavResult::Exists && w.systemError() == EEXIST);
        assert(read(path) == std::vector<unsigned char>({'p','r','e','s','e','r','v','e'}));
        assert(read(path+".part").size() == 52 && w.abort() == WavResult::Ok);
    }
    for (const char* name : {"valid.wav","max.wav","existing.wav","partial.wav.part","collision.wav","collision.wav.part","aborted.wav.part","short.wav.part","sync.wav.part","rename.wav.part","fat-collision.wav","fat-collision.wav.part"})
        assert(::unlink((root+"/"+name).c_str()) == 0);
    assert(::rmdir(root.c_str()) == 0);
    std::cout << "wav_writer_tests: all assertions passed\n";
}
