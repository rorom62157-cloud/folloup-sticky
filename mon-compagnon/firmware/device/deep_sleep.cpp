// SPDX-License-Identifier: GPL-3.0-only
#include "deep_sleep.hpp"
#include "board.hpp"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "Deep sleep rocker wake is verified only for ESP32-S3"
#endif

namespace compagnon::device {
namespace {
constexpr gpio_num_t kWakePins[] = {
    static_cast<gpio_num_t>(kButtonUp),
    static_cast<gpio_num_t>(kButtonSelect),
    static_cast<gpio_num_t>(kButtonDown)
};
constexpr uint64_t kWakeMask = (1ULL << kButtonUp) |
                              (1ULL << kButtonSelect) |
                              (1ULL << kButtonDown);
bool check(esp_err_t rc, const char* action, std::string& error) {
    if (rc == ESP_OK) return true;
    error = std::string(action) + ": " + esp_err_to_name(rc);
    return false;
}
}  // namespace

bool armDeepSleep(std::string& error) {
    error.clear();
    // ALL removes timers, ULP, touch and other wake sources. The modern EXT1
    // API appends pins, so also clear its retained pin mask explicitly.
    if (!check(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL),
               "Desactivation des reveils", error) ||
        !check(esp_sleep_disable_ext1_wakeup_io(0),
               "Reinitialisation des entrees de reveil", error)) return false;
    // Keep the RTC IO domain powered so the internal active-low key pullups
    // remain effective throughout sleep; do not depend on digital pullups.
    if (!check(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON),
               "Alimentation des entrees RTC", error)) return false;
    for (const auto pin : kWakePins) {
        if (!check(rtc_gpio_pullup_en(pin), "Pull-up de reveil", error) ||
            !check(rtc_gpio_pulldown_dis(pin), "Pull-down de reveil", error)) return false;
    }
    // IDF switches these pads to RTC inputs immediately before sleep. Keep
    // their digital mux intact here so release polling and failure UI work.
    return check(esp_sleep_enable_ext1_wakeup_io(kWakeMask, ESP_EXT1_WAKEUP_ANY_LOW),
                 "Activation du reveil molette", error);
}

bool wakeKeysReleased() {
    for (const auto pin : kWakePins) {
        if (gpio_get_level(pin) == 0) return false;
    }
    return true;
}

[[noreturn]] void enterDeepSleep() { esp_deep_sleep_start(); }

bool wokeFromDeepSleep() {
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
}
}  // namespace compagnon::device
