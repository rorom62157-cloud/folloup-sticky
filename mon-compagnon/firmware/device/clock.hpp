#pragma once
#include <cstdint>
#include <ctime>
#include <string>
namespace compagnon::device {
struct ClockSnapshot {
    bool valid = false;
    std::string date, time, timezone, source;
};
// Process-global POSIX TZ: owner serializes all calls, including localtime.
bool configureClockTimezone(const std::string& timezone);
// Pure parsing/conversion helper. Rejects DST gaps AND repeated local minutes.
bool parseClockLocal(const std::string& date, const std::string& time, std::time_t& epoch);
// Pure PCF85063A 24-hour decode; reads seconds..years, rejects OS/bad BCD.
bool decodeRtcUtc(const std::uint8_t* seven_bytes, std::time_t& epoch);
// RTC provenance supplied from persisted settings: never infer its timezone.
// Uses audio.hpp shared IDF I2C bus; no network or automatic NTP.
bool beginClock(bool rtc_utc_known, std::string& error);
// Success means both system clock and RTC updated. On RTC I/O failure system
// remains valid but persistence failed: do not mark RTC provenance as saved.
bool setClockLocal(const std::string& date, const std::string& time, std::string& error);
ClockSnapshot clockSnapshot();
} // namespace compagnon::device
