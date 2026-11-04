// SPDX-License-Identifier: GPL-3.0-only
// Narrow adaptation of FolloUp AXP2101 IRQ/getter logic at
// 1682b7e7d7ff9c6b23ab7e903c44b241f095b402. See docs/audio-v03.md,
// components/axp2101/{axp2101.cc,xpowers_axp2101_driver.cc,
// xpowers_axp2101_constants.h,xpowers_axp2101_params.h}.
#include "power_key.hpp"
#include "audio.hpp"
#include "driver/gpio.h"
#include <array>

namespace compagnon::device {
namespace {
i2c_master_dev_handle_t pmic = nullptr;
bool ready = false, configuredHold = false;
std::array<PowerKeyEvent,4> events{};
size_t eventRead=0,eventCount=0;
std::string lastError;
constexpr gpio_num_t kIrq = GPIO_NUM_38;
constexpr uint8_t kChipId = 0x03, kIrqEnable = 0x41, kIrqStatus = 0x49;
constexpr uint8_t kShort = 0x08, kLong = 0x04, kKeyBits = 0x0F;
constexpr uint8_t kPressed = 0x02, kReleased = 0x01;
bool check(esp_err_t rc,const char* action,std::string& error) {
    if (rc == ESP_OK) return true;
    error=std::string(action)+": "+esp_err_to_name(rc); return false;
}
bool read(uint8_t reg,uint8_t& value,std::string& error) {
    return check(i2c_master_transmit_receive(pmic,&reg,1,&value,1,30),"Lecture PMIC",error);
}
bool write(uint8_t reg,uint8_t value,std::string& error) {
    const uint8_t data[]={reg,value};
    return check(i2c_master_transmit(pmic,data,sizeof(data),30),"IRQ PMIC",error);
}
}
bool beginPowerKey(std::string& error,bool holdRecording) {
    error.clear();
    if (ready && (!holdRecording || configuredHold)) return true;
    i2c_master_bus_handle_t bus=nullptr;
    if (!ensureSharedI2cBus(bus,error)) return false;
    std::lock_guard<std::mutex> lock(sharedI2cMutex());
    if (!pmic) {
        i2c_device_config_t config{}; config.dev_addr_length=I2C_ADDR_BIT_LEN_7;
        config.device_address=0x34; config.scl_speed_hz=400000;
        if (!check(i2c_master_bus_add_device(bus,&config,&pmic),"Peripherique PMIC",error)) return false;
    }
    uint8_t id=0,enabled=0,pending=0;
    if (!read(kChipId,id,error)) return false;
    if (id != 0x4A) { error="PMIC non confirme AXP2101: commande PWR indisponible"; return false; }
    gpio_config_t pin{}; pin.pin_bit_mask=1ULL<<kIrq; pin.mode=GPIO_MODE_INPUT;
    pin.pull_up_en=GPIO_PULLUP_ENABLE;
    if (!check(gpio_config(&pin),"Entree IRQ PMIC",error)) return false;
    // Drop only stale key status before accepting new application gestures.
    // W1C: never write the complete status byte or clear unrelated IRQ sources.
    if (!read(kIrqStatus,pending,error)) return false;
    if ((pending&kKeyBits) && !write(kIrqStatus,pending&kKeyBits,error)) return false;
    if (!read(kIrqEnable,enabled,error)) return false;
    if ((enabled&kKeyBits)!=kKeyBits &&
        !write(kIrqEnable,enabled|kKeyBits,error)) return false;
    uint8_t verified=0;
    if (!read(kIrqEnable,verified,error)) return false;
    if ((verified&kKeyBits)!=kKeyBits) { error="IRQ PWR non activees"; return false; }
    if (holdRecording) {
        // Manufacturer AXP2101 datasheet: OFFLEVEL shutdown enable is 22[1];
        // separate 16s shutdown enable is 10[2]. 27 holds timing, not enable.
        // Never touch thermal shutdown 22[2], charger or peripheral rails.
        uint8_t common=0,off=0;
        if (!read(0x10,common,error) || !read(0x22,off,error)) return false;
        // Avoid replaying a self-clearing software reset/poweroff command.
        if (common&0x03) { error="Commande PMIC en cours; configuration PWR refusee"; return false; }
        const uint8_t newCommon=common&uint8_t(~0x04),newOff=off&uint8_t(~0x02);
        bool ok=true;
        if (newOff!=off) ok=write(0x22,newOff,error);
        if (ok && newCommon!=common) ok=write(0x10,newCommon,error);
        uint8_t actualCommon=0,actualOff=0;
        if (ok) ok=read(0x10,actualCommon,error) && read(0x22,actualOff,error);
        if (ok && (actualCommon!=newCommon || actualOff!=newOff)) {
            error="Verification maintien PWR echouee"; ok=false;
        }
        if (!ok) {
            // Best effort rollback to verified original bytes on failure.
            std::string rollbackError;
            const bool restoredCommon=write(0x10,common,rollbackError);
            const bool restoredOff=write(0x22,off,rollbackError);
            if (!restoredCommon || !restoredOff) error+="; restauration PMIC non confirmee";
            return false;
        }
        configuredHold=true;
    }
    eventRead=eventCount=0;
    ready=true; lastError.clear(); return true;
}
PowerKeyEvent pollPowerKey() {
    if (!ready) return PowerKeyEvent::None;
    // Drain every event from an acknowledged packet before reading another.
    // At most four key bits exist, so release cannot be dropped by Short/Long.
    if (eventRead<eventCount) return events[eventRead++];
    if (gpio_get_level(kIrq)!=0) return PowerKeyEvent::None;
    std::lock_guard<std::mutex> lock(sharedI2cMutex());
    uint8_t pending=0; lastError.clear();
    if (!read(kIrqStatus,pending,lastError)) return PowerKeyEvent::None;
    const uint8_t keys=pending&kKeyBits;
    if (!keys) return PowerKeyEvent::None;
    // Clear only observed key bits; existing non-key IRQ owners retain theirs.
    if (!write(kIrqStatus,keys,lastError)) return PowerKeyEvent::None;
    eventRead=eventCount=0;
    // PWRON is pulled to GND by the key: negative edge is press, positive
    // edge is release. FolloUp logging labels those edges backwards.
    if (keys&kPressed) events[eventCount++]=PowerKeyEvent::Pressed;
    if (keys&kLong) events[eventCount++]=PowerKeyEvent::Long;
    if (keys&kShort) events[eventCount++]=PowerKeyEvent::Short;
    if (keys&kReleased) events[eventCount++]=PowerKeyEvent::Released;
    return events[eventRead++];
}
std::string powerKeyError() { return lastError; }
bool requestPowerOff(std::string& error) {
    error.clear();
    if (!ready) { error="PMIC non initialise"; return false; }
    std::lock_guard<std::mutex> lock(sharedI2cMutex());
    uint8_t common=0;
    if (!read(0x10,common,error)) return false;
    if (common&0x02) { error="Redemarrage PMIC en cours"; return false; }
    // FolloUp shutdown() / manufacturer REG10[0]: soft power-off request.
    // No readback: a successful request can remove power before another I/O.
    return write(0x10,common|0x01,error);
}
BatterySnapshot readBatterySnapshot(std::string& error) {
    BatterySnapshot result; error.clear();
    if (!ready) { error="PMIC non initialise"; return result; }
    std::lock_guard<std::mutex> lock(sharedI2cMutex());
    uint8_t status1=0,status2=0,percent=0;
    if (!read(0x00,status1,error) || !read(0x01,status2,error)) return result;
    result.available=true; result.batteryPresent=(status1&0x08)!=0;
    result.charging=(status2>>5)==1;
    if (result.batteryPresent) {
        if (!read(0xA4,percent,error)) { result.available=false; return result; }
        if (percent<=100) result.percent=percent;
    }
    return result;
}
} // namespace compagnon::device
