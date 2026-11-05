// SPDX-License-Identifier: GPL-3.0-only
// I2S/pins adapted from FolloUp Sticky, pinned 1682b7e7d7ff9c6b23ab7e903c44b241f095b402.
// ES8311 register driver is Espressif Apache-2.0; see audio_vendor/NOTICE.md.
#include "audio.hpp"
#include "wav_writer.hpp"
#include "audio_levels.hpp"
#include "audio_vendor/wav_metadata.hpp"
#include <algorithm>
#include <atomic>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// Arduino's flat sketch builder does not build arbitrary vendor subdirectories.
extern "C" {
#include "audio_vendor/es8311.inc"
}

namespace compagnon::device {
namespace {
std::mutex busMutex;
i2c_master_bus_handle_t sensorBus = nullptr;
i2s_chan_handle_t tx = nullptr, rx = nullptr;
es8311_handle_t codec = nullptr;
AudioEngine* owner = nullptr;
std::atomic<bool> captureOverflow{false};
bool IRAM_ATTR receiveOverflow(i2s_chan_handle_t, i2s_event_data_t*, void*) {
    captureOverflow.store(true,std::memory_order_relaxed); return false;
}
constexpr gpio_num_t kPa = GPIO_NUM_39;
bool check(esp_err_t rc, const char* operation, std::string& error) {
    if (rc == ESP_OK) return true;
    error = std::string(operation) + ": " + esp_err_to_name(rc); return false;
}
using audio_detail::safeAudioPath;
using audio_detail::wavInfo;
void cleanupHardware() {
    gpio_set_level(kPa,0);
    if (codec) { es8311_delete(codec); codec = nullptr; }
    if (rx) { (void)i2s_channel_disable(rx); (void)i2s_del_channel(rx); rx = nullptr; }
    if (tx) { (void)i2s_channel_disable(tx); (void)i2s_del_channel(tx); tx = nullptr; }
}
}
std::mutex& sharedI2cMutex() { return busMutex; }
bool ensureSharedI2cBus(i2c_master_bus_handle_t& bus, std::string& error) {
    std::lock_guard<std::mutex> lock(busMutex);
    if (!sensorBus) {
        i2c_master_bus_config_t cfg{};
        cfg.i2c_port = I2C_NUM_1; cfg.sda_io_num = GPIO_NUM_41; cfg.scl_io_num = GPIO_NUM_42;
        cfg.clk_source = I2C_CLK_SRC_DEFAULT; cfg.glitch_ignore_cnt = 7;
        cfg.flags.enable_internal_pullup = true;
        // Refuse an independently occupied controller; never assume its pins.
        if (!check(i2c_new_master_bus(&cfg,&sensorBus),"Bus I2C partage",error)) return false;
    }
    bus = sensorBus; return true;
}
bool AudioEngine::begin(std::string& error) {
    error.clear();
    if (ready_) return true;
    if (owner && owner != this) { error = "Audio deja possede"; return false; }
    struct stat st{};
    if (stat("/sdcard",&st) != 0 || !S_ISDIR(st.st_mode)) { error="SD non montee"; return false; }
    if (mkdir(kAudioDirectory,0700) != 0 && errno != EEXIST) { error="Creation repertoire audio impossible"; return false; }
    if (stat(kAudioDirectory,&st) != 0 || !S_ISDIR(st.st_mode)) { error="Repertoire audio invalide"; return false; }
    i2c_master_bus_handle_t bus;
    if (!ensureSharedI2cBus(bus,error)) return false;
    std::lock_guard<std::mutex> i2cLock(busMutex);
    gpio_config_t pa{}; pa.pin_bit_mask = 1ULL << kPa; pa.mode = GPIO_MODE_OUTPUT;
    if (!check(gpio_config(&pa),"Amplificateur",error)) return false;
    gpio_set_level(kPa,0);
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    channel.dma_desc_num = 8; channel.dma_frame_num = 256;
    channel.auto_clear_after_cb = true;
    if (!check(i2s_new_channel(&channel,&tx,&rx),"I2S0 duplex",error)) return false;
    i2s_std_config_t config{};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO);
    config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    config.gpio_cfg.mclk=GPIO_NUM_13; config.gpio_cfg.bclk=GPIO_NUM_14;
    config.gpio_cfg.ws=GPIO_NUM_47; config.gpio_cfg.dout=GPIO_NUM_48; config.gpio_cfg.din=GPIO_NUM_21;
    if (!check(i2s_channel_init_std_mode(tx,&config),"I2S sortie",error) ||
        !check(i2s_channel_init_std_mode(rx,&config),"I2S entree",error) ||
        !check(i2s_channel_enable(tx),"Horloge I2S",error)) { cleanupHardware(); return false; }
    i2s_event_callbacks_t callbacks{}; callbacks.on_recv_q_ovf=receiveOverflow;
    if (!check(i2s_channel_register_event_callback(rx,&callbacks,nullptr),"Surveillance capture",error)) { cleanupHardware(); return false; }
    codec = es8311_create(bus,0x18);
    es8311_clock_config_t clock{}; clock.mclk_from_mclk_pin=true;
    clock.mclk_frequency=4096000; clock.sample_frequency=16000;
    if (!codec) { error="Creation codec impossible"; cleanupHardware(); return false; }
    if (!check(es8311_init(codec,&clock,ES8311_RESOLUTION_16,ES8311_RESOLUTION_16),"Codec ES8311",error) ||
        !check(es8311_microphone_config(codec,false),"Microphone analogique",error) ||
        !check(es8311_write_reg(codec,ES8311_ADC_REG17,0xBF),"Niveau ADC neutre",error) ||
        !check(es8311_microphone_gain_set(codec,ES8311_MIC_GAIN_30DB),"Gain microphone",error) ||
        !check(es8311_write_reg(codec,ES8311_DAC_REG32,audioVolumeRegister(70)),"Volume neutre",error) ||
        !check(es8311_voice_mute(codec,false),"Sortie codec",error)) { cleanupHardware(); return false; }
    // TX remains enabled with automatic zero clearing: drives shared clocks,
    // with PA off except playback. RX is enabled only for an active capture.
    owner=this;
    TaskHandle_t handle=nullptr;
    status_.state=AudioState::Idle;
    if (xTaskCreate(taskEntry,"compagnon-audio",8192,this,5,&handle) != pdPASS) {
        error="Creation tache audio impossible"; cleanupHardware(); owner=nullptr; return false;
    }
    task_=handle; ready_=true; return true;
}
bool AudioEngine::setVolume(int percent,std::string& error) {
    error.clear();const int reg=audioVolumeRegister(percent);
    if(!ready_||reg<0){error="Volume indisponible ou invalide";return false;}
    std::lock_guard<std::mutex> lock(busMutex);
    return check(es8311_write_reg(codec,ES8311_DAC_REG32,reg),"Volume ES8311",error)&&check(es8311_voice_mute(codec,percent==0),"Muet ES8311",error);
}
bool AudioEngine::setMicrophoneGain(int db,std::string& error){
    error.clear();std::lock_guard<std::mutex> stateLock(mutex_);
    if(!ready_||db<0||db>42||db%6||(status_.state!=AudioState::Idle&&status_.state!=AudioState::Fault)){error="Gain indisponible pendant l’audio";return false;}
    std::lock_guard<std::mutex> busLock(busMutex);
    return check(es8311_microphone_gain_set(codec,static_cast<es8311_mic_gain_t>(db/6)),"Gain microphone",error);
}
AudioStatus AudioEngine::status() const { std::lock_guard<std::mutex> lock(mutex_); return status_; }
bool AudioEngine::startRecording(std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_); error.clear();
    if (!ready_ || (status_.state != AudioState::Idle && status_.state != AudioState::Fault)) { error="Audio occupe ou indisponible"; return false; }
    status_.state=AudioState::Starting; status_.error.clear(); status_.durationMs=0;
    pendingPath_.clear(); stop_=false; xTaskNotifyGive(static_cast<TaskHandle_t>(task_)); return true;
}
void AudioEngine::stopRecording() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state == AudioState::Recording || status_.state == AudioState::Starting) stop_=true;
}
bool AudioEngine::play(const std::string& path,std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_); error.clear();
    if (!ready_ || (status_.state != AudioState::Idle && status_.state != AudioState::Fault)) { error="Audio occupe ou indisponible"; return false; }
    if (!safeAudioPath(path)) { error="Chemin audio refuse"; return false; }
    pendingPath_=path; stop_=false; status_.state=AudioState::Starting;
    status_.durationMs=0; status_.error.clear(); xTaskNotifyGive(static_cast<TaskHandle_t>(task_)); return true;
}
void AudioEngine::stopPlayback() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.state == AudioState::Playing || (status_.state == AudioState::Starting && !pendingPath_.empty())) stop_=true;
}
void AudioEngine::taskEntry(void* self) { static_cast<AudioEngine*>(self)->run(); }
void AudioEngine::run() {
    std::array<uint8_t,2048> pcm{};
    for (;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        std::string path,error;
        { std::lock_guard<std::mutex> lock(mutex_); path=pendingPath_; }
        auto stopped=[this] { std::lock_guard<std::mutex> lock(mutex_); return stop_; };
        auto update=[this](AudioState state,uint32_t ms) { std::lock_guard<std::mutex> lock(mutex_); status_.state=state; status_.durationMs=ms; };
        if (path.empty()) {
            WavWriter writer;
            WavResult beginResult=WavResult::Exists;
            for (unsigned attempt=0; attempt<8 && beginResult==WavResult::Exists; ++attempt) {
                char name[96]; snprintf(name,sizeof(name),"%s/rec-%08lx%08lx%08lx%08lx.wav",kAudioDirectory,
                    (unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
                path=name; beginResult=writer.begin(path);
            }
            if (beginResult != WavResult::Ok) error="Ouverture SD audio impossible";
            bool rxEnabled=false;
            captureOverflow.store(false,std::memory_order_relaxed);
            if (error.empty()) rxEnabled=check(i2s_channel_enable(rx),"Capture I2S",error);
            const int64_t started=esp_timer_get_time();
            if (error.empty()) update(AudioState::Recording,0);
            while (error.empty() && !stopped() && writer.dataBytesWritten()<WavWriter::max_data_bytes && esp_timer_get_time()-started<240000000LL) {
                size_t count=0;
                const size_t requested=std::min(pcm.size(),WavWriter::max_data_bytes-writer.dataBytesWritten());
                const auto rc=i2s_channel_read(rx,pcm.data(),requested,&count,150);
                if (captureOverflow.load(std::memory_order_relaxed)) { error="SD trop lente: capture interrompue"; break; }
                if (rc != ESP_OK || !count || count%2) { error="Lecture microphone interrompue"; break; }
                if (writer.append(pcm.data(),count)!=WavResult::Ok) { error="Ecriture SD audio interrompue"; break; }
                update(AudioState::Recording,uint32_t(writer.dataBytesWritten()/32));
            }
            if (rxEnabled) (void)i2s_channel_disable(rx);
            if (captureOverflow.load(std::memory_order_relaxed) && error.empty()) error="SD trop lente: capture interrompue";
            const auto duration=uint32_t(writer.dataBytesWritten()/32);
            update(AudioState::Saving,duration);
            // Even on capture failure publish all valid captured samples if the
            // writer is still usable. I/O faults keep the original .part intact.
            bool saved=false;
            if (writer.state()==WavState::Writing && writer.dataBytesWritten()>0) {
                saved=writer.finish()==WavResult::Ok;
                if (!saved) error="Sauvegarde SD non confirmee; fichier .part conserve";
            } else if (writer.state()!=WavState::Idle) { (void)writer.abort(); if (error.empty()) error="Aucun echantillon; fichier .part conserve"; }
            std::lock_guard<std::mutex> lock(mutex_);
            if (saved) status_.lastSavedPath=path;
            status_.error=error; status_.state=error.empty()?AudioState::Idle:AudioState::Fault;
        } else {
            FILE* file=fopen(path.c_str(),"rb"); uint32_t remaining=0,played=0;
            if (!file || !wavInfo(file,remaining)) error="WAV local invalide ou inaccessible";
            if (error.empty() && !stopped()) { gpio_set_level(kPa,1); update(AudioState::Playing,0); }
            while (error.empty() && remaining && !stopped()) {
                const auto count=std::min<size_t>(pcm.size(),remaining);
                if (fread(pcm.data(),1,count,file)!=count) { error="Lecture SD interrompue"; break; }
                size_t written=0;
                if (i2s_channel_write(tx,pcm.data(),count,&written,150)!=ESP_OK || written!=count) { error="Lecture haut-parleur interrompue"; break; }
                remaining-=count; played+=count; update(AudioState::Playing,played/32);
            }
            // One complete DMA ring (8*256 frames) takes128ms; allow queued
            // samples to drain on natural EOF only; a stop silences immediately.
            if (error.empty() && !remaining && !stopped()) vTaskDelay(pdMS_TO_TICKS(130));
            gpio_set_level(kPa,0);
            // Keep clocks running and flush any queued tail silently before
            // accepting a new command (auto-clear emits zeros after the tail).
            if (remaining || !error.empty() || stopped()) vTaskDelay(pdMS_TO_TICKS(130));
            if (file) fclose(file);
            std::lock_guard<std::mutex> lock(mutex_);
            status_.error=error; status_.state=error.empty()?AudioState::Idle:AudioState::Fault;
        }
    }
}
bool AudioEngine::eraseRecording(const std::string& path,std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);error.clear();
    if(!ready_||(status_.state!=AudioState::Idle&&status_.state!=AudioState::Fault)){error="Audio occupé";return false;}
    if(!safeAudioPath(path)){error="Chemin audio refusé";return false;}
    FILE* file=fopen(path.c_str(),"rb");uint32_t bytes=0;bool valid=file&&wavInfo(file,bytes);if(file)fclose(file);
    if(!valid){error="Original audio invalide ou absent";return false;}
    if(std::remove(path.c_str())!=0){error="Suppression de l’audio échouée";return false;}
    if(status_.lastSavedPath==path)status_.lastSavedPath.clear();return true;
}
bool AudioEngine::listRecordings(std::vector<AudioRecording>& result,std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_); result.clear(); error.clear();
    if (!ready_ || (status_.state!=AudioState::Idle && status_.state!=AudioState::Fault)) { error="Liste indisponible pendant audio"; return false; }
    DIR* dir=opendir(kAudioDirectory);
    if (!dir) { error="Lecture repertoire audio impossible"; return false; }
    while (auto* entry=readdir(dir)) {
        const std::string path=std::string(kAudioDirectory)+"/"+entry->d_name;
        if (!safeAudioPath(path)) continue;
        FILE* file=fopen(path.c_str(),"rb"); uint32_t bytes=0;
        if(file&&wavInfo(file,bytes)){struct stat st{};uint64_t modified=fstat(fileno(file),&st)==0&&st.st_mtime>0?static_cast<uint64_t>(st.st_mtime):0;result.push_back({path,bytes/32,modified});}
        if (file) fclose(file);
        if (result.size()==128) break;
    }
    closedir(dir);
    std::sort(result.begin(),result.end(),[](const AudioRecording& a,const AudioRecording& b) { return a.modifiedEpoch!=b.modifiedEpoch?a.modifiedEpoch>b.modifiedEpoch:a.path<b.path; });
    return true;
}
} // namespace compagnon::device
