#include "agenda_store.hpp"
#include "clock.hpp"
#include <cstdlib>
#include <iostream>
#include <map>
using namespace compagnon;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)
struct MemoryFiles : NoteFiles {
    std::map<std::string, std::string> disk;
    int fail_step = 0, step = 0;
    bool after = false, short_write = false, read_failure = false;
    int writes = 0;
    bool fault() { return ++step == fail_step; }
    NoteIoResult read(const std::string& name, std::size_t limit, std::string& bytes) override {
        if (read_failure) return NoteIoResult::Error;
        const auto it = disk.find(name);
        if (it == disk.end()) return NoteIoResult::Missing;
        if (it->second.size() > limit) return NoteIoResult::Error;
        bytes = it->second; return NoteIoResult::Ok;
    }
    NoteIoResult remove(const std::string& name) override {
        const bool bad = fault();
        if (bad && !after) return NoteIoResult::Error;
        const bool existed = disk.erase(name) != 0;
        if (bad) return NoteIoResult::Error;
        return existed ? NoteIoResult::Ok : NoteIoResult::Missing;
    }
    NoteIoResult writeExclusive(const std::string& name, const std::string& bytes) override {
        ++writes;
        const bool bad = fault();
        if (bad && !after) return NoteIoResult::Error;
        if (disk.count(name)) return NoteIoResult::Exists;
        // Partial transfer/ENOSPC or sync/close failure; bytes may remain.
        disk[name] = bad && short_write ? bytes.substr(0, bytes.size() / 2) : bytes;
        return bad ? NoteIoResult::Error : NoteIoResult::Ok;
    }
    NoteIoResult renameAbsent(const std::string& from, const std::string& to) override {
        const bool bad = fault();
        if (bad && !after) return NoteIoResult::Error;
        if (disk.count(to)) return NoteIoResult::Exists; // FAT semantics.
        if (!disk.count(from)) return NoteIoResult::Missing;
        disk[to] = disk.at(from); disk.erase(from);
        return bad ? NoteIoResult::Error : NoteIoResult::Ok;
    }
    void faultsOff() { fail_step = 0; step = 0; after = false; short_write = false; }
};
void dates() {
    for (const auto& s : {"2000-02-29","2024-02-29","2099-12-31"}) CHECK(validAgendaDate(s));
    for (const auto& s : {"1900-02-29","2100-01-01","2023-02-29","2024-04-31","2024-00-01","2024-01-00","2024-1-01","abcd-ef-gh"}) CHECK(!validAgendaDate(s));
    CHECK(validAgendaTime("00:00") && validAgendaTime("23:59"));
    CHECK(!validAgendaTime("24:00") && !validAgendaTime("12:60") && !validAgendaTime("1:00"));
}
void store() {
    MemoryFiles files; AgendaStore agenda(files); AgendaEvent e;
    CHECK(agenda.create({"a","","2024-01-01","12:00",false})==NoteResult::NotReady);
    CHECK(agenda.recover()==NoteResult::Ok);
    CHECK(agenda.create({"Midi","ligne\nAGENDA1\n😀","2024-01-01","12:00",false},&e)==NoteResult::Ok);
    CHECK(e.details=="ligne\nAGENDA1\n😀" && e.version==1);
    CHECK(agenda.update(e.id,{"Edit","","2024-01-01","14:00",false},0)==NoteResult::Conflict);
    CHECK(agenda.update(e.id,{"Edit","","2024-01-01","14:00",false},1,&e)==NoteResult::Ok);
    CHECK(e.version==2 && e.time=="14:00");
    CHECK(agenda.create({"Journee","","2024-01-01","",true})==NoteResult::Ok);
    CHECK(agenda.create({"Matin","","2024-01-01","09:00",false})==NoteResult::Ok);
    CHECK(agenda.create({"Demain","","2024-01-02","08:00",false})==NoteResult::Ok);
    auto list=agenda.day("2024-01-01");
    CHECK(list.size()==3 && list[0].all_day && list[1].time=="09:00" && list[2].time=="14:00");
    list=agenda.upcoming("2024-01-01","10:00");
    CHECK(list.size()==3 && list[0].all_day && list[1].title=="Edit" && list[2].title=="Demain");
    CHECK(agenda.upcoming("2024-01-01","10:00",1).size()==1);
    CHECK(agenda.day("bad").empty());
    AgendaStore reboot(files); CHECK(reboot.recover()==NoteResult::Ok && reboot.events().size()==4);
    CHECK(reboot.erase(e.id,1)==NoteResult::Conflict);
    CHECK(reboot.erase(e.id,2)==NoteResult::Ok);
    const auto writes=files.writes;
    for (auto d : {AgendaDraft{" ","","2024-01-01","12:00",false}, AgendaDraft{"a","","2024-02-30","12:00",false},
        AgendaDraft{"a","","2024-01-01","",false},AgendaDraft{"a","","2024-01-01","12:00",true},
        AgendaDraft{"a",std::string(4001,'x'),"2024-01-01","12:00",false},AgendaDraft{"\xc0\xaf","","2024-01-01","12:00",false}})
        CHECK(reboot.create(d)==NoteResult::InvalidArgument);
    CHECK(files.writes==writes);
    while(reboot.events().size()<32) CHECK(reboot.create({"Full",std::string(4000,'x'),"2024-01-01","12:00",false})==NoteResult::Ok);
    CHECK(reboot.create({"Limit","","2024-01-01","12:00",false})==NoteResult::LimitExceeded);
    AgendaStore full(files); CHECK(full.recover()==NoteResult::Ok && full.events().size()==32);
}
void corruption() {
    // Valid checksummed notes that are not agenda envelopes must fail closed.
    for (const auto& body : {std::string(""),std::string("AGENDA1\n2024-01-01\n12:00\n0"),
        std::string("AGENDA1\n2024-02-30\n12:00\n0\n"),std::string("AGENDA1\n2024-01-01\n12:00\n1\n")}) {
        MemoryFiles files; NoteStore raw(files); CHECK(raw.recover()==NoteResult::Ok);
        CHECK(raw.create({"Title",body,false,""})==NoteResult::Ok);
        const auto snapshot=files.disk; AgendaStore agenda(files);
        CHECK(agenda.recover()==NoteResult::Corrupt && !agenda.ready() && agenda.events().empty());
        CHECK(files.disk==snapshot && agenda.erase("n1",1)==NoteResult::NotReady);
    }
    MemoryFiles base; AgendaStore initial(base); CHECK(initial.recover()==NoteResult::Ok);
    CHECK(initial.create({"Original","","2024-01-01","12:00",false})==NoteResult::Ok);
    for(int fail=1;fail<=4;++fail) for(bool after:{false,true}) {
        auto files=base; files.faultsOff(); AgendaStore agenda(files); CHECK(agenda.recover()==NoteResult::Ok);
        files.fail_step=fail; files.after=after; files.short_write=true;
        CHECK(agenda.create({"Next","","2024-01-02","12:00",false})==NoteResult::IoError);
        CHECK(!agenda.ready() && agenda.events().empty());
        files.faultsOff(); AgendaStore reboot(files); CHECK(reboot.recover()==NoteResult::Ok);
        CHECK(reboot.events().size()==((fail==4 && after)?2:1));
    }
}
void clockParsing() {
    using namespace compagnon::device;
    CHECK(configureClockTimezone("Europe/Madrid"));
    std::time_t winter=0, summer=0, sentinel=42;
    CHECK(parseClockLocal("2026-01-01","12:00",winter));
    CHECK(parseClockLocal("2026-07-01","12:00",summer));
    std::tm t{}; CHECK(gmtime_r(&winter,&t) && t.tm_hour==11);
    CHECK(gmtime_r(&summer,&t) && t.tm_hour==10);
    CHECK(!parseClockLocal("2026-03-29","02:30",sentinel) && sentinel==42);
    CHECK(!parseClockLocal("2026-10-25","02:30",sentinel));
    CHECK(!configureClockTimezone("invalid"));
    CHECK(configureClockTimezone("UTC"));
    CHECK(parseClockLocal("2026-03-29","02:30",sentinel));
    std::uint8_t rtc[]={0x59,0x58,0x23,0x29,0x04,0x02,0x24};
    std::time_t epoch; CHECK(decodeRtcUtc(rtc,epoch));
    CHECK(gmtime_r(&epoch,&t) && t.tm_year==124 && t.tm_mon==1 && t.tm_mday==29 && t.tm_hour==23);
    rtc[0]|=0x80; CHECK(!decodeRtcUtc(rtc,epoch)); rtc[0]=0x6a; CHECK(!decodeRtcUtc(rtc,epoch));
    rtc[0]=0; rtc[6]=0x23; CHECK(!decodeRtcUtc(rtc,epoch));
}
int main() { dates(); store(); corruption(); clockParsing(); std::cout<<"agenda and clock parsing tests passed\n"; }
