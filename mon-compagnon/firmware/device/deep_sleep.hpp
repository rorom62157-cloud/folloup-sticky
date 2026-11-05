// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>

namespace compagnon::device {
// Single application owner. Finish display/storage/audio and stop radios first.
// Configures only GPIO4/5/6 active-low EXT1 wake; never enters sleep on failure.
bool armDeepSleep(std::string& error);
// Read just before entry; all rocker contacts must have been released.
bool wakeKeysReleased();
// Call only after successful arm and a release check. Does not return: wake boots.
[[noreturn]] void enterDeepSleep();
// Observed EXT1 cause only; no persistent counter or pin configuration changes.
// Startup must rtc_gpio_deinit GPIO4/5/6 before using them as digital inputs.
bool wokeFromDeepSleep();
}  // namespace compagnon::device
