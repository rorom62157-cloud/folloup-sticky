// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "driver/i2c_master.h"

namespace compagnon::device {
inline constexpr char kAudioDirectory[] = "/sdcard/compagnon-audio";
enum class AudioState { Unavailable, Idle, Starting, Recording, Saving, Playing, Fault };
struct AudioStatus {
    AudioState state = AudioState::Unavailable;
    uint32_t durationMs = 0;
    std::string lastSavedPath;
    std::string error;
};
struct AudioRecording { std::string path; uint32_t durationMs = 0; uint64_t modifiedEpoch = 0; };
// Shared sensor bus. All compound sensor/PMIC/codec operations must hold this
// mutex; ensureSharedI2cBus takes it internally, so call it before locking.
std::mutex& sharedI2cMutex();
bool ensureSharedI2cBus(i2c_master_bus_handle_t& bus, std::string& error);

// One application owner calls commands/list; status is thread-safe. Object must
// live for application lifetime. begin requires SD already mounted; never formats.
class AudioEngine {
public:
    bool begin(std::string& error);
    bool setMicrophoneGain(int db, std::string& error);
    bool setVolume(int percent, std::string& error);
    bool startRecording(std::string& error);
    void stopRecording();
    bool play(const std::string& path, std::string& error);
    void stopPlayback();
    AudioStatus status() const;
    // Only idle; bounded to 128 valid, finalized WAVs; .part never published.
    bool eraseRecording(const std::string& path, std::string& error);
    bool listRecordings(std::vector<AudioRecording>& result, std::string& error);
private:
    static void taskEntry(void* self);
    void run();
    mutable std::mutex mutex_;
    AudioStatus status_;
    bool ready_ = false, stop_ = false;
    std::string pendingPath_;
    void* task_ = nullptr;
};
} // namespace compagnon::device
