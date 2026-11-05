// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
namespace compagnon::device {
enum class PowerKeyEvent { None, Short, Long, Pressed, Released };
// Verified AXP2101 only. holdRecording=true additionally disables both PWR
// hardware long-hold shutdown paths (0x22 bit1, 0x10 bit2), with readback.
// Charger/rails/thermal protections/timing unchanged. Call once at startup.
// Default false preserves hardware shutdown configuration.
bool beginPowerKey(std::string& error, bool holdRecording = false);
// Single application owner. None also on I/O failure; inspect powerKeyError().
PowerKeyEvent pollPowerKey();
std::string powerKeyError();
// Caller must obtain explicit confirmation and finish all SD writes first.
// True means I2C request accepted, never proof that rails have powered off.
bool requestPowerOff(std::string& error);
struct BatterySnapshot {
    bool available = false;
    bool batteryPresent = false;
    int percent = -1;
    bool charging = false;
};
// Read-only, uncalibrated PMIC report; percent=-1 if absent/invalid.
BatterySnapshot readBatterySnapshot(std::string& error);
} // namespace compagnon::device
