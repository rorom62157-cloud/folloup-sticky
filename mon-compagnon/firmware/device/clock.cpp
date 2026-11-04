#include "clock.hpp"
#include "agenda_store.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef ARDUINO
#include "audio.hpp"
#include "driver/i2c_master.h"
#include <mutex>
#include <sys/time.h>
#endif
namespace compagnon::device {
namespace {
std::string zone = "Europe/Madrid";
#ifdef ARDUINO
bool trusted = false;
std::string source;
i2c_master_dev_handle_t rtc = nullptr;
#endif
int part(const std::string& s, std::size_t at, std::size_t count) {
    return std::atoi(s.substr(at,count).c_str());
}
bool same(const std::tm& a, const std::tm& b) {
    return a.tm_year == b.tm_year && a.tm_mon == b.tm_mon && a.tm_mday == b.tm_mday &&
        a.tm_hour == b.tm_hour && a.tm_min == b.tm_min && a.tm_sec == b.tm_sec;
}
// UTC Gregorian conversion independent of process TZ (2000..2099).
std::time_t utcEpoch(const std::tm& t) {
    std::int64_t days = 10957; // Days from 1970-01-01 to 2000-01-01.
    for (int y = 2000; y < t.tm_year + 1900; ++y) days += y % 4 == 0 ? 366 : 365;
    static const int md[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (int m = 0; m < t.tm_mon; ++m) days += md[m] + (m == 1 && (t.tm_year+1900)%4 == 0);
    days += t.tm_mday - 1;
    return static_cast<std::time_t>(days * 86400 + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
}
int bcd(unsigned char b) {
    return (b & 15) <= 9 && (b >> 4) <= 9 ? (b >> 4)*10+(b & 15) : -1;
}
#ifdef ARDUINO
bool readRegisters(std::uint8_t reg, std::uint8_t* data, std::size_t size) {
    return i2c_master_transmit_receive(rtc,&reg,1,data,size,250) == ESP_OK;
}
bool writeRegisters(const std::uint8_t* data, std::size_t size) {
    return i2c_master_transmit(rtc,data,size,250) == ESP_OK;
}
std::uint8_t toBcd(int v) { return static_cast<std::uint8_t>((v/10)*16+v%10); }
bool systemTime(std::time_t t) {
    timeval tv{}; tv.tv_sec = t;
    return settimeofday(&tv,nullptr) == 0;
}
#endif
}
bool configureClockTimezone(const std::string& timezone) {
    const char* tz = nullptr;
    if (timezone == "Europe/Madrid") tz = "CET-1CEST,M3.5.0/2,M10.5.0/3";
    else if (timezone == "UTC") tz = "UTC0";
    else return false;
    if (setenv("TZ",tz,1) != 0) return false;
    tzset(); zone = timezone; return true;
}
bool parseClockLocal(const std::string& date, const std::string& time, std::time_t& epoch) {
    if (!validAgendaDate(date) || !validAgendaTime(time)) return false;
    std::tm original{};
    original.tm_year = part(date,0,4)-1900; original.tm_mon = part(date,5,2)-1;
    original.tm_mday = part(date,8,2); original.tm_hour = part(time,0,2); original.tm_min = part(time,3,2);
    bool found = false; std::time_t candidate = 0;
    for (int dst : {0,1}) {
        auto probe = original; probe.tm_isdst = dst;
        const auto value = mktime(&probe);
        std::tm back{};
        if (!localtime_r(&value,&back) || !same(back,original)) continue;
        if (found && candidate != value) return false; // Ambiguous autumn fold.
        candidate = value; found = true;
    }
    if (found) epoch = candidate;
    return found;
}
bool decodeRtcUtc(const std::uint8_t* r, std::time_t& epoch) {
    if (!r || (r[0]&0x80) || (r[1]&0x80) || (r[2]&0xc0) ||
        (r[3]&0xc0) || r[4]>6 || (r[5]&0xe0)) return false;
    int v[7]; for (int i=0;i<7;++i) v[i] = bcd(r[i]);
    if (v[0]<0 || v[0]>59 || v[1]<0 || v[1]>59 || v[2]<0 || v[2]>23 ||
        v[3]<1 || v[5]<1 || v[5]>12 || v[6]<0) return false;
    char date[11]; std::snprintf(date,sizeof(date),"%04d-%02d-%02d",2000+v[6],v[5],v[3]);
    if (!validAgendaDate(date)) return false;
    std::tm t{}; t.tm_year=100+v[6]; t.tm_mon=v[5]-1; t.tm_mday=v[3];
    t.tm_hour=v[2]; t.tm_min=v[1]; t.tm_sec=v[0];
    epoch = utcEpoch(t); return true;
}
#ifdef ARDUINO
bool beginClock(bool known, std::string& error) {
    error.clear(); trusted = false; source.clear();
    if (!configureClockTimezone(zone)) { error="Fuseau horaire indisponible"; return false; }
    i2c_master_bus_handle_t bus = nullptr;
    if (!ensureSharedI2cBus(bus,error)) return false;
    std::lock_guard<std::mutex> lock(sharedI2cMutex());
    if (!rtc) {
        i2c_device_config_t config{}; config.dev_addr_length=I2C_ADDR_BIT_LEN_7;
        config.device_address=0x51; config.scl_speed_hz=100000;
        if (i2c_master_bus_add_device(bus,&config,&rtc) != ESP_OK) { error="RTC: ajout I2C impossible"; return false; }
    }
    if (!known) { error="Horloge a regler: provenance UTC RTC inconnue"; return false; }
    std::uint8_t control=0, values[7]{};
    if (!readRegisters(0,&control,1) || !readRegisters(4,values,7)) { error="RTC: lecture I2C impossible"; return false; }
    std::time_t epoch;
    if ((control & 0xa2) || !decodeRtcUtc(values,epoch)) { error="RTC arretee, format incompatible ou date invalide"; return false; }
    if (!systemTime(epoch)) { error="Horloge systeme indisponible"; return false; }
    trusted=true; source="rtc"; return true;
}
bool setClockLocal(const std::string& date, const std::string& time, std::string& error) {
    error.clear(); std::time_t epoch;
    if (!parseClockLocal(date,time,epoch)) { error="Date/heure invalide ou ambigue (changement heure)"; return false; }
    // UTC must fit RTC's two-digit 2000..2099 year even at timezone edges.
    std::tm t{};
    if (!gmtime_r(&epoch,&t) || t.tm_year<100 || t.tm_year>199) { error="Date UTC hors plage RTC"; return false; }
    if (!systemTime(epoch)) { error="Reglage horloge systeme impossible"; return false; }
    trusted=true; source="manual";
    if (!rtc) { error="Heure systeme reglee, RTC indisponible"; return false; }
    std::lock_guard<std::mutex> lock(sharedI2cMutex());
    std::uint8_t control;
    if (!readRegisters(0,&control,1)) { error="Heure systeme reglee, lecture RTC impossible"; return false; }
    // Preserve control bits; STOP counters while writing, force 24h mode.
    const std::uint8_t stop[] = {0,static_cast<std::uint8_t>((control & ~0x82)|0x20)};
    const std::uint8_t data[] = {4,toBcd(t.tm_sec),toBcd(t.tm_min),toBcd(t.tm_hour),toBcd(t.tm_mday),
        static_cast<std::uint8_t>(t.tm_wday),toBcd(t.tm_mon+1),toBcd(t.tm_year-100)};
    const std::uint8_t start[] = {0,static_cast<std::uint8_t>(control & ~0xa2)};
    const bool stopped=writeRegisters(stop,sizeof(stop));
    const bool written=stopped && writeRegisters(data,sizeof(data));
    // Leave STOP set after a failed date transfer: never bless a partial date.
    const bool started=written && writeRegisters(start,sizeof(start));
    if (!written || !started) { error="Heure systeme reglee, sauvegarde RTC echouee"; return false; }
    std::uint8_t verify[7]{}; std::time_t persisted;
    if (!readRegisters(4,verify,7) || !decodeRtcUtc(verify,persisted) ||
        persisted < epoch || persisted > epoch+2) {
        error="Heure systeme reglee, verification RTC echouee"; return false;
    }
    return true;
}
ClockSnapshot clockSnapshot() {
    ClockSnapshot result; result.timezone=zone; result.source=source;
    if (!trusted) return result;
    const auto now=std::time(nullptr); std::tm t{};
    if (!localtime_r(&now,&t) || t.tm_year<100 || t.tm_year>199) return result;
    char date[11], time[6];
    if (!std::strftime(date,sizeof(date),"%Y-%m-%d",&t) || !std::strftime(time,sizeof(time),"%H:%M",&t)) return result;
    result.valid=true; result.date=date; result.time=time; return result;
}
#endif
} // namespace compagnon::device
