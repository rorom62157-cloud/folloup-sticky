#pragma once
#include "note_store.hpp"

namespace compagnon {
struct AgendaDraft {
    std::string title, details, date, time;
    bool all_day = false;
};
struct AgendaEvent {
    std::string id, title, details, date, time;
    bool all_day = false;
    std::uint64_t version = 0;
};
// Gregorian local civil dates; no implicit timezone, remote sync or reminders.
bool validAgendaDate(const std::string& date);
bool validAgendaTime(const std::string& time);
// Caller serializes access. Dedicated NoteFiles directory /compagnon-agenda.
// I/O failures invalidate readiness: explicitly recover, never retry creates.
class AgendaStore {
public:
    static constexpr std::size_t max_events = 32;
    static constexpr std::size_t max_title_bytes = NoteStore::max_title_bytes;
    static constexpr std::size_t max_details_bytes = 4000;
    static constexpr std::uint64_t max_version = NoteStore::max_version;
    explicit AgendaStore(NoteFiles& files) : store_(files) {}
    NoteResult recover();
    bool ready() const { return valid_ && store_.ready(); }
    std::vector<AgendaEvent> events() const;
    std::vector<AgendaEvent> day(const std::string& date) const;
    // Inclusive now; today's all-day events remain included throughout the day.
    std::vector<AgendaEvent> upcoming(const std::string& date, const std::string& time,
                                      std::size_t limit = max_events) const;
    NoteResult create(const AgendaDraft& draft, AgendaEvent* saved = nullptr);
    NoteResult update(const std::string& id, const AgendaDraft& draft,
                      std::uint64_t expected_version, AgendaEvent* saved = nullptr);
    NoteResult erase(const std::string& id, std::uint64_t expected_version);
private:
    NoteStore store_;
    bool valid_ = false;
};
} // namespace compagnon
