// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include "wav_writer.hpp"
namespace compagnon::device::audio_detail {
inline uint32_t le32(const uint8_t* p) { return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
inline bool safeAudioPath(const std::string& path) {
    const std::string prefix = std::string("/sdcard/compagnon-audio") + "/";
    if (path.compare(0,prefix.size(),prefix) != 0) return false;
    const auto name = path.substr(prefix.size());
    // Generated names: rec- + 32 random hex digits + .wav. Reject traversal.
    if (name.size() != 40 || name.substr(0,4) != "rec-" || name.substr(36) != ".wav") return false;
    return name.find_first_not_of("0123456789abcdef",4) == 36;
}
inline bool wavInfo(FILE* file, uint32_t& bytes) {
    uint8_t h[44];
    if (fread(h,1,44,file) != 44 || memcmp(h,"RIFF",4) || memcmp(h+8,"WAVEfmt ",8) ||
        le32(h+16) != 16 || h[20] != 1 || h[21] || h[22] != 1 || h[23] ||
        le32(h+24) != 16000 || le32(h+28) != 32000 || h[32] != 2 || h[33] ||
        h[34] != 16 || h[35] || memcmp(h+36,"data",4)) return false;
    bytes = le32(h+40);
    if (!bytes || bytes % 2 || bytes > WavWriter::max_data_bytes || le32(h+4) != 36+bytes) return false;
    struct stat st{};
    return fstat(fileno(file),&st) == 0 && st.st_size == off_t(44+bytes);
}
}
