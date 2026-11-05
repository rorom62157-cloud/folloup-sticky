#include "agenda_store.hpp"
#include <algorithm>

namespace compagnon {
namespace {
bool digits(const std::string& s, std::size_t at, std::size_t count) {
    for (std::size_t i = at; i < at + count; ++i) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}
unsigned number(const std::string& s, std::size_t at, std::size_t count) {
    unsigned n = 0;
    for (std::size_t i = at; i < at + count; ++i) n = n * 10 + unsigned(s[i] - '0');
    return n;
}
bool nonblank(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i++]);
        unsigned cp = c, n = 0;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; n = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; n = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; n = 3; }
        else if (c >= 0x80) return true;
        if (s.size() - i < n) return true;
        while (n--) cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 63);
        if (!(cp == 0x20 || (cp >= 9 && cp <= 13) || cp == 0x85 || cp == 0xa0 ||
              cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 ||
              cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000)) return true;
    }
    return false; // NoteStore validates UTF-8, lengths and NUL separately.
}
bool valid(const AgendaDraft& d) {
    return nonblank(d.title) && d.details.size() <= AgendaStore::max_details_bytes &&
        validAgendaDate(d.date) && (d.all_day ? d.time.empty() : validAgendaTime(d.time));
}
std::string encode(const AgendaDraft& d) {
    return "AGENDA1\n" + d.date + "\n" + (d.all_day ? "-----" : d.time) + "\n" +
        (d.all_day ? "1\n" : "0\n") + d.details;
}
bool decode(const Note& n, AgendaEvent& event) {
    const auto& s = n.text;
    if (n.pinned || !n.audio_id.empty() || s.size() < 27 || s.compare(0, 8, "AGENDA1\n") ||
        s[18] != '\n' || s[24] != '\n' || s[26] != '\n' || (s[25] != '0' && s[25] != '1')) return false;
    const bool all = s[25] == '1';
    if (all && s.substr(19, 5) != "-----") return false;
    AgendaDraft d{n.title, s.substr(27), s.substr(8, 10), all ? "" : s.substr(19, 5), all};
    if (!valid(d)) return false;
    event = {n.id, d.title, d.details, d.date, d.time, d.all_day, n.version};
    return true;
}
NoteResult output(NoteResult result, const Note& n, AgendaEvent* saved) {
    if (result == NoteResult::Ok && saved) decode(n, *saved);
    return result;
}
} // namespace
bool validAgendaDate(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-' || !digits(s,0,4) ||
        !digits(s,5,2) || !digits(s,8,2)) return false;
    const auto y = number(s,0,4), m = number(s,5,2), d = number(s,8,2);
    if (y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1) return false;
    const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return d <= days[m-1] + (m == 2 && y % 4 == 0 ? 1 : 0);
}
bool validAgendaTime(const std::string& s) {
    return s.size() == 5 && s[2] == ':' && digits(s,0,2) && digits(s,3,2) &&
        number(s,0,2) <= 23 && number(s,3,2) <= 59;
}
NoteResult AgendaStore::recover() {
    valid_ = false;
    const auto result = store_.recover();
    if (result != NoteResult::Ok) return result;
    AgendaEvent e;
    for (const auto& n : store_.notes()) if (!decode(n,e)) return NoteResult::Corrupt;
    valid_ = true;
    return NoteResult::Ok;
}
std::vector<AgendaEvent> AgendaStore::events() const {
    std::vector<AgendaEvent> result;
    if (!ready()) return result;
    for (const auto& n : store_.notes()) { AgendaEvent e; decode(n,e); result.push_back(e); }
    std::sort(result.begin(), result.end(), [](const AgendaEvent& a, const AgendaEvent& b) {
        if (a.date != b.date) return a.date < b.date;
        if (a.all_day != b.all_day) return a.all_day;
        if (a.time != b.time) return a.time < b.time;
        return a.id < b.id;
    });
    return result;
}
std::vector<AgendaEvent> AgendaStore::day(const std::string& date) const {
    if (!validAgendaDate(date)) return {};
    auto result = events();
    result.erase(std::remove_if(result.begin(), result.end(), [&](const AgendaEvent& e) {return e.date != date;}), result.end());
    return result;
}
std::vector<AgendaEvent> AgendaStore::upcoming(const std::string& date, const std::string& time, std::size_t limit) const {
    if (!validAgendaDate(date) || !validAgendaTime(time)) return {};
    auto result = events();
    result.erase(std::remove_if(result.begin(), result.end(), [&](const AgendaEvent& e) {
        return e.date < date || (e.date == date && !e.all_day && e.time < time);
    }), result.end());
    if (result.size() > limit) result.resize(limit);
    return result;
}
NoteResult AgendaStore::create(const AgendaDraft& d, AgendaEvent* saved) {
    if (!ready()) return NoteResult::NotReady;
    if (!valid(d)) return NoteResult::InvalidArgument;
    Note n;
    return output(store_.create({d.title,encode(d),false,""}, &n), n, saved);
}
NoteResult AgendaStore::update(const std::string& id, const AgendaDraft& d, std::uint64_t version, AgendaEvent* saved) {
    if (!ready()) return NoteResult::NotReady;
    if (!valid(d)) return NoteResult::InvalidArgument;
    Note n;
    return output(store_.update(id,{d.title,encode(d),false,""},version,&n),n,saved);
}
NoteResult AgendaStore::erase(const std::string& id, std::uint64_t version) {
    return ready() ? store_.erase(id,version) : NoteResult::NotReady;
}
} // namespace compagnon
