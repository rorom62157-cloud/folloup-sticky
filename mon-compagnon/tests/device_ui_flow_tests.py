#!/usr/bin/env python3
"""Run the actual firmware navigation functions on host with SDK/IO fixtures.

This validates controller transitions, not physical buttons or the panel. The
explicit source seams fail if the firmware is reorganized rather than testing a
second handwritten UI implementation.
"""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/device/device.ino').read_text()
def section(start, end):
    assert source.count(start) == 1 and source.count(end) == 1, (start, end)
    return source[source.index(start):source.index(end)]
preamble = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "raster.hpp"
#include "ink.hpp"
#include "agenda_store.hpp"
#include "postit_http.hpp"
#include "clock.hpp"
#include "power_key.hpp"
#include "local_files.hpp"
#include "task_store.hpp"
#include "note_service.hpp"
#include "compagnon.hpp"
using namespace compagnon; using namespace compagnon::device;
using QueueHandle_t = void*; constexpr int pdTRUE=1, HIGH=1, LOW=0, WIFI_AP=1, WIFI_OFF=0, ESP_OK=0;
unsigned long ticks=1000; unsigned long millis(){return ticks;} void delay(int ms){ticks+=ms;}
int digitalRead(int){return HIGH;} void digitalWrite(int,int){}
int xQueueSend(void*,const void*,int){return 1;} int xQueueReceive(void*,void*,int){return 0;}
int esp_vfs_fat_info(const char*,uint64_t*total,uint64_t*free){*total=100;*free=50;return 0;}
struct Pref {
 unsigned char value=1; bool failWrites=false; std::vector<bool> rtcWrites;
 size_t putUChar(const char*,unsigned char v){value=v;return 1;}
 size_t putBool(const char* key,bool v){if(std::string(key)=="rtc-utc")rtcWrites.push_back(v);return failWrites?0:1;}
 bool getBool(const char*,bool fallback){return fallback;}
 unsigned getUInt(const char*,unsigned fallback){return fallback;}
 size_t putString(const char*,const char*){return 1;}
 std::string getString(const char*,const char* fallback){return fallback;}
} preferences;
constexpr int pdPASS=1,portMAX_DELAY=0;
int xTaskCreate(void(*)(void*),const char*,int,void*,int,void*){return pdPASS;}
void vTaskDelete(void*){} bool runHttpSelfTest(std::string&){return false;}
namespace compagnon::device {
 enum class AudioState {Unavailable,Idle,Starting,Recording,Saving,Playing,Fault};
 struct AudioStatus {AudioState state=AudioState::Unavailable;uint32_t durationMs=0;std::string lastSavedPath,error;};
 struct AudioRecording {std::string path;uint32_t durationMs=0;uint64_t modifiedEpoch=0;};
 struct AudioEngine {
  AudioStatus fixture;int startCalls=0,stopCalls=0,playCalls=0,stopPlayCalls=0;bool acceptCommands=false;
  AudioStatus status()const{return fixture;}
  bool startRecording(std::string&){++startCalls;return acceptCommands;}
  bool eraseRecording(const std::string&,std::string&){return false;}
  void stopRecording(){++stopCalls;}void stopPlayback(){++stopPlayCalls;}
  bool play(const std::string&,std::string&){++playCalls;return acceptCommands;}
  bool setVolume(int,std::string&){return false;}bool setMicrophoneGain(int,std::string&){return false;}
  bool listRecordings(std::vector<AudioRecording>&,std::string&){return false;}
 };
 bool audioDiagnostics(const std::string&,std::string&){return false;}
 ClockSnapshot clockFixture;bool clockSetResult=false;int clockSetCalls=0;
 bool setClockLocal(const std::string&,const std::string&,std::string&){++clockSetCalls;return clockSetResult;}
 ClockSnapshot clockSnapshot(){return clockFixture;}
 bool configureClockTimezone(const std::string&){return false;}
 bool requestPowerOff(std::string&){return false;}
 bool wakeKeysReleased(){return true;} bool armDeepSleep(std::string&){return false;} void enterDeepSleep(){}
 PowerKeyEvent pollPowerKey(){return PowerKeyEvent::None;}
 BatterySnapshot readBatterySnapshot(std::string&){return {};}
}
struct Wifi {void mode(int){} bool softAP(const char*,const char*,int,bool,int){return true;} void softAPdisconnect(bool){}} WiFi;
struct SerialFixture {bool available(){return false;} int read(){return 0;} void println(const char*){} void flush(){} void setTimeout(int){} std::string readStringUntil(char){return {};} template<class...T>void printf(const char*,T...){} } Serial;
struct MemoryFiles : NoteFiles {
 std::map<std::string,std::string> data;
 NoteIoResult read(const std::string&n,size_t max,std::string&out)override{auto it=data.find(n);if(it==data.end())return NoteIoResult::Missing;if(it->second.size()>max)return NoteIoResult::Error;out=it->second;return NoteIoResult::Ok;}
 NoteIoResult writeExclusive(const std::string&n,const std::string&v)override{if(data.count(n))return NoteIoResult::Exists;data[n]=v;return NoteIoResult::Ok;}
 NoteIoResult remove(const std::string&n)override{return data.erase(n)?NoteIoResult::Ok:NoteIoResult::Missing;}
 NoteIoResult renameAbsent(const std::string&a,const std::string&b)override{if(data.count(b))return NoteIoResult::Exists;if(!data.count(a))return NoteIoResult::Missing;data[b]=data[a];data.erase(a);return NoteIoResult::Ok;}
};
MemoryFiles noteFilesFixture,taskFilesFixture,agendaFilesFixture,recordingFilesFixture,postitFilesFixture;
PostitStore hostPostits(postitFilesFixture);
NoteStore hostNotes(noteFilesFixture);
struct ServerFixture {
 NoteService service; explicit ServerFixture(NoteStore&s):service(s){}
 template<class F> NoteServiceResponse localNotes(F f){return f(service);}
 template<class F> NoteResult localPostits(F f){return f(hostPostits);}
 Result physicalDecision(bool,uint64_t){return Result::Ok;} void revoke(){}
 bool approved(){return false;} uint64_t pendingRequestId(){return 0;} int begin(){return 0;}
};
std::unique_ptr<ServerFixture> server;
std::unique_ptr<TaskStore> taskStore;
LocalFiles browser("/private/tmp");
Orientation orientation=Orientation::Portrait;
void storageTest(){}
'''
code = preamble + section('std::unique_ptr<AgendaStore> agendaStore;', 'std::unique_ptr<NoteStore> store;')
code += section('std::vector<Note> notes;', 'int width()')
code += section('int width()', 'void displayWorker(')
code += section('struct Button {', 'std::string viewTitle()')
code += section('std::string viewTitle()', 'void storageTest()')
code += section('void loop() {', '\nEND_TEST_SEAM') if '\nEND_TEST_SEAM' in source else source[source.index('void loop() {'):]
code += r'''
void ready(View target){view=target;dirty=false;displayBusy=false;cursor=0;queuedConfirm=0;showingApproval=false;lastInput=ticks;}
int main(){
 std::vector<uint8_t> a(Raster::bytes),b(Raster::bytes);frame=a.data();spare=b.data();displayResults=reinterpret_cast<void*>(1);displayJobs=reinterpret_cast<void*>(2);
 assert(hostPostits.recover()==NoteResult::Ok);assert(hostNotes.recover()==NoteResult::Ok);server.reset(new ServerFixture(hostNotes));taskStore.reset(new TaskStore(taskFilesFixture));assert(taskStore->recover()==NoteResult::Ok);
 agendaStore.reset(new AgendaStore(agendaFilesFixture));assert(agendaStore->recover()==NoteResult::Ok);
 recordingIndex.reset(new NoteStore(recordingFilesFixture));assert(recordingIndex->recover()==NoteResult::Ok);
 ready(View::Home);assert(labels().size()==6);cursor=5;input(1);assert(view==View::Settings);
 input(3);assert(view==View::Home);
 ready(View::Home);cursor=2;input(1);assert(view==View::Apps);
 const View appViews[]={View::Agenda,View::Notes,View::Pins,View::Tasks,View::Audio,View::Files,View::Network,View::Help};
 for(int i=0;i<8;++i){ready(View::Apps);assert(labels().size()==8);cursor=i;input(1);assert(view==appViews[i]&&view!=View::Settings);}
 ready(View::Apps);input(2);input(2);input(2);assert(cursor==3);displayBusy=true;input(1);assert(view==View::Apps&&queuedConfirm);
 displayBusy=false;dirty=false;loop();assert(view==View::Tasks); // Confirmation waits for visible state.
 ready(View::Apps);input(2);input(1);assert(queuedConfirm);input(2);assert(!queuedConfirm); // Navigation cancels an earlier queued click.
 ready(View::Agenda);input(1);assert(view==View::Editor&&edit==Edit::EventTitle);
 draftTitle="Visite médicale";dirty=false;input(1);assert(view==View::EventSchedule&&eventDraft.title==draftTitle);
 dirty=false;input(1);assert(view==View::DatePicker&&!settingClock);
 numberYear=2026;numberMonth=11;numberDay=19;cursor=3;dirty=false;input(1);assert(view==View::EventSchedule&&eventDraft.date=="2026-11-19");
 cursor=1;dirty=false;input(1);assert(view==View::TimePicker);numberHour=14;numberMinute=30;cursor=2;dirty=false;input(1);
 assert(view==View::EventSchedule&&eventDraft.time=="14:30");cursor=4;dirty=false;input(1);assert(view==View::Event&&agenda.size()==1);
 AgendaStore reopenedAgenda(agendaFilesFixture);assert(reopenedAgenda.recover()==NoteResult::Ok);assert(reopenedAgenda.events().front().title=="Visite médicale"&&reopenedAgenda.events().front().date=="2026-11-19"&&reopenedAgenda.events().front().time=="14:30");
 ready(View::Agenda);input(2);input(1);assert(queuedConfirm&&view==View::Agenda);dirty=false;loop();assert(view==View::Event);
 ready(View::Notes);input(1);assert(view==View::Editor&&titleField);
 draftTitle="Une idee";dirty=false;input(1);assert(!titleField&&view==View::Editor);
 draftText="Texte conserve";dirty=false;input(1);assert(view==View::Note&&selected.title=="Une idee"&&hostNotes.notes().size()==1);
 ready(View::Note);input(1);assert(view==View::NoteActions);dirty=false;input(1);assert(view==View::Note&&selected.pinned);
 ready(View::NoteActions);cursor=2;input(1);assert(view==View::Editor&&titleField);draftTitle="Titre modifie";dirty=false;input(1);assert(selected.title=="Titre modifie"&&selected.text=="Texte conserve");
 ready(View::NoteActions);cursor=3;input(1);assert(view==View::DeleteNote);dirty=false;input(1);assert(view==View::Note&&hostNotes.notes().size()==1); // Default is Cancel.
 ready(View::Tasks);input(1);draftTitle="A faire";dirty=false;input(1);draftText="Details";dirty=false;input(1);assert(view==View::Task&&!selectedTask.done);
 ready(View::TaskActions);input(1);assert(selectedTask.done);TaskStore afterRestart(taskFilesFixture);assert(afterRestart.recover()==NoteResult::Ok&&afterRestart.tasks().front().done);
 ready(View::Notes);input(1);draftTitle="Brouillon";dirty=false;input(3);assert(view==View::Discard);dirty=false;input(1);assert(view==View::Editor&&draftTitle=="Brouillon");
 dirty=false;input(3);dirty=false;cursor=1;input(1);assert(view==View::Notes&&draftTitle.empty());
 ready(View::DisplaySettings);input(1);assert(orientation==Orientation::PortraitInverted&&preferences.value==2);
 ready(View::Help);input(2);assert(page==1);
 ready(View::Editor);draftTitle="é";titleField=true;key=2;input(1);assert(draftTitle.empty());
 // Audio is a command/state fixture: no captured samples, codec or WAV claims.
 ready(View::Home);cursor=3;input(1);assert(view==View::Audio&&audio.startCalls==1);
 audio.fixture.state=AudioState::Recording;ready(View::Audio);input(1);assert(audio.stopCalls==1);
 audio.fixture.state=AudioState::Saving;ready(View::Audio);input(1);assert(audio.startCalls==1&&audio.stopCalls==1);
 audio.fixture.state=AudioState::Playing;ready(View::Audio);input(1);assert(audio.stopPlayCalls==1);
 selectedAudio={"/fixture/audio.wav",1234};ready(View::AudioActions);input(1);assert(audio.playCalls==1);
 ready(View::AudioActions);cursor=1;input(1);assert(view==View::AudioType);cursor=1;dirty=false;input(1);assert(view==View::AudioNext);
 cursor=2;dirty=false;input(1);assert(view==View::Editor&&edit==Edit::EventTitle&&eventDraft.details.find(selectedAudio.path)!=std::string::npos);
 assert(recordingIndex->notes().size()==1); // Local classification only.
 preferences.rtcWrites.clear();clockSetResult=false;assert(!applyClock("invalid","12:00"));
 assert(preferences.rtcWrites==std::vector<bool>{false}); // Failed clock update never marks UTC provenance true.
 preferences.rtcWrites.clear();preferences.failWrites=true;const int calls=clockSetCalls;
 assert(!applyClock("2026-11-19","12:00")&&clockSetCalls==calls);preferences.failWrites=false;
 ready(View::DatePicker);settingClock=true;numberYear=2026;numberMonth=2;numberDay=30;cursor=3;
 const auto writes=preferences.rtcWrites.size();input(1);assert(view==View::DatePicker&&preferences.rtcWrites.size()==writes);
 // State changes cancel a queued Stop rather than starting a new recording.
 audio.fixture.state=AudioState::Idle;audioState.state=AudioState::Playing;ready(View::Audio);displayBusy=true;input(1);assert(queuedConfirm);
 const int startBefore=audio.startCalls;loop();assert(!queuedConfirm);displayBusy=false;dirty=false;loop();assert(audio.startCalls==startBefore);
 // An editor interrupted by capture is retained across a related creation form.
 ready(View::Editor);draftTitle="Brouillon intact";draftText="Texte important";edit=Edit::NoteText;titleField=false;audio.fixture.state=AudioState::Idle;audio.acceptCommands=true;
 toggleRecording();assert(!suspendedDrafts.empty());go(View::Audio);startEdit(Edit::NewTask);draftTitle="Autre titre";
 ready(View::AudioType);input(3);assert(view==View::Editor&&draftTitle=="Brouillon intact"&&draftText=="Texte important");
 ready(View::DeleteAudio);input(1);assert(view==View::AudioActions);
 ready(View::AudioType);cursor=6;input(1);assert(view==View::Audio&&message.find("annulé")!=std::string::npos);
 ready(View::PowerConfirm);input(1);assert(view==View::Settings&&!pendingPowerOff);
 // Committed shutdown blocks a new capture and aborts if live audio is active.
 pendingPowerOff=true;const auto commandsBefore=audio.startCalls;toggleRecording();assert(audio.startCalls==commandsBefore);
 view=View::PowerDown;audio.fixture.state=AudioState::Recording;audioState=audio.fixture;dirty=false;displayBusy=false;lastSensorPoll=ticks;lastAudioPaint=ticks;lastPoll=ticks;loop();assert(!pendingPowerOff&&view==View::Audio&&server);
 // Long lower-button action exposes a safe explicit menu, preserving drafts.
 ready(View::Home);openPowerMenu();assert(view==View::PowerMenu&&labels().size()==3);
 ready(View::Editor);draftTitle="Encore en cours";openPowerMenu();assert(view==View::Editor);draftTitle.clear();draftText.clear();
 ready(View::PowerMenu);audio.fixture.state=AudioState::Recording;input(1);assert(!pendingDeepSleep&&view==View::PowerMenu);
 ready(View::PowerMenu);audio.fixture.state=AudioState::Idle;input(1);assert(pendingDeepSleep&&view==View::DeepScreen);
 const auto beforeDeep=audio.startCalls;toggleRecording();assert(audio.startCalls==beforeDeep);
 pendingDeepSleep=false;
 // Pin presentation persists alignment without changing original note text.
 selected=hostNotes.notes().front();assert(selected.pinned);loadPostitStyle(selected.id);
 auto original=selected.text;ready(View::NoteActions);cursor=5;input(1);assert(selectedStyle.alignment==PostitAlignment::Right&&selected.text==original);
 PostitStyle storedStyle;assert(hostPostits.get(selected.id,storedStyle)==NoteResult::Ok&&storedStyle.alignment==PostitAlignment::Right);
 std::puts("Actual device UI controller flow tests passed (host fixtures, not hardware).");
}
'''
stage=root/'build/host';stage.mkdir(parents=True,exist_ok=True)
cpp=stage/'device_ui_flow_tests.cpp';cpp.write_text(code)
subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I'+str(root/'core'),'-I'+str(root/'firmware/device'),str(cpp),str(root/'firmware/device/raster.cpp'),str(root/'firmware/device/ink.cpp'),str(root/'core/agenda_store.cpp'),str(root/'core/postit_store.cpp'),str(root/'firmware/device/local_files.cpp'),str(root/'core/compagnon.cpp'),str(root/'core/note_store.cpp'),str(root/'core/note_service.cpp'),str(root/'core/task_store.cpp'),'-o',str(stage/'device_ui_flow_tests')],check=True)
subprocess.run([str(stage/'device_ui_flow_tests')],check=True)
