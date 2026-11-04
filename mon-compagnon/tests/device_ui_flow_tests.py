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
#include "local_files.hpp"
#include "task_store.hpp"
#include "note_service.hpp"
#include "compagnon.hpp"
using namespace compagnon; using namespace compagnon::device;
using QueueHandle_t = void*; constexpr int pdTRUE=1, HIGH=1, LOW=0, WIFI_AP=1, ESP_OK=0;
unsigned long ticks=1000; unsigned long millis(){return ticks;} void delay(int ms){ticks+=ms;}
int digitalRead(int){return HIGH;}
int xQueueSend(void*,const void*,int){return 1;} int xQueueReceive(void*,void*,int){return 0;}
int esp_vfs_fat_info(const char*,uint64_t*total,uint64_t*free){*total=100;*free=50;return 0;}
struct Pref {unsigned char value=1;size_t putUChar(const char*,unsigned char v){value=v;return 1;}} preferences;
struct Wifi {void mode(int){} bool softAP(const char*,const char*,int,bool,int){return true;} void softAPdisconnect(bool){}} WiFi;
struct SerialFixture {bool available(){return false;} int read(){return 0;} void println(const char*){}} Serial;
struct MemoryFiles : NoteFiles {
 std::map<std::string,std::string> data;
 NoteIoResult read(const std::string&n,size_t max,std::string&out)override{auto it=data.find(n);if(it==data.end())return NoteIoResult::Missing;if(it->second.size()>max)return NoteIoResult::Error;out=it->second;return NoteIoResult::Ok;}
 NoteIoResult writeExclusive(const std::string&n,const std::string&v)override{if(data.count(n))return NoteIoResult::Exists;data[n]=v;return NoteIoResult::Ok;}
 NoteIoResult remove(const std::string&n)override{return data.erase(n)?NoteIoResult::Ok:NoteIoResult::Missing;}
 NoteIoResult renameAbsent(const std::string&a,const std::string&b)override{if(data.count(b))return NoteIoResult::Exists;if(!data.count(a))return NoteIoResult::Missing;data[b]=data[a];data.erase(a);return NoteIoResult::Ok;}
};
MemoryFiles noteFilesFixture,taskFilesFixture;
NoteStore hostNotes(noteFilesFixture);
struct ServerFixture {
 NoteService service; explicit ServerFixture(NoteStore&s):service(s){}
 template<class F> NoteServiceResponse localNotes(F f){return f(service);}
 Result physicalDecision(bool,uint64_t){return Result::Ok;} void revoke(){}
 bool approved(){return false;} uint64_t pendingRequestId(){return 0;} int begin(){return 0;}
};
std::unique_ptr<ServerFixture> server;
std::unique_ptr<TaskStore> taskStore;
LocalFiles browser("/private/tmp");
Orientation orientation=Orientation::Portrait;
void storageTest(){}
'''
code = preamble + section('std::vector<Note> notes;', 'int width()')
code += section('int width()', 'void displayWorker(')
code += section('struct Button {', 'std::string viewTitle()')
code += section('std::string viewTitle()', 'void storageTest()')
code += section('void loop() {', '\nEND_TEST_SEAM') if '\nEND_TEST_SEAM' in source else source[source.index('void loop() {'):]
code += r'''
void ready(View target){view=target;dirty=false;displayBusy=false;cursor=0;queuedConfirm=0;showingApproval=false;lastInput=ticks;}
int main(){
 std::vector<uint8_t> a(Raster::bytes),b(Raster::bytes);frame=a.data();spare=b.data();displayResults=reinterpret_cast<void*>(1);displayJobs=reinterpret_cast<void*>(2);
 assert(hostNotes.recover()==NoteResult::Ok);server.reset(new ServerFixture(hostNotes));taskStore.reset(new TaskStore(taskFilesFixture));assert(taskStore->recover()==NoteResult::Ok);
 ready(View::Home);input(1);assert(view==View::Apps);
 ready(View::Apps);input(2);input(2);assert(cursor==2);displayBusy=true;input(1);assert(view==View::Apps&&queuedConfirm);
 displayBusy=false;dirty=false;loop();assert(view==View::Tasks); // Confirmation waits for visible state.
 ready(View::Apps);input(2);input(1);assert(queuedConfirm);input(2);assert(!queuedConfirm); // Navigation cancels an earlier queued click.
 ready(View::Home);cursor=1;input(1);assert(view==View::Editor&&titleField);
 draftTitle="Une idee";dirty=false;input(1);assert(!titleField&&view==View::Editor);
 draftText="Texte conserve";dirty=false;input(1);assert(view==View::Note&&selected.title=="Une idee"&&hostNotes.notes().size()==1);
 ready(View::Note);input(1);assert(view==View::NoteActions);dirty=false;input(1);assert(view==View::Note&&selected.pinned);
 ready(View::NoteActions);cursor=2;input(1);assert(view==View::Editor&&titleField);draftTitle="Titre modifie";dirty=false;input(1);assert(selected.title=="Titre modifie"&&selected.text=="Texte conserve");
 ready(View::NoteActions);cursor=3;input(1);assert(view==View::DeleteNote);dirty=false;input(1);assert(view==View::Note&&hostNotes.notes().size()==1); // Default is Cancel.
 ready(View::Home);cursor=2;input(1);draftTitle="A faire";dirty=false;input(1);draftText="Details";dirty=false;input(1);assert(view==View::Task&&!selectedTask.done);
 ready(View::TaskActions);input(1);assert(selectedTask.done);TaskStore afterRestart(taskFilesFixture);assert(afterRestart.recover()==NoteResult::Ok&&afterRestart.tasks().front().done);
 ready(View::Home);cursor=1;input(1);draftTitle="Brouillon";dirty=false;input(3);assert(view==View::Discard);dirty=false;input(1);assert(view==View::Editor&&draftTitle=="Brouillon");
 dirty=false;input(3);dirty=false;cursor=1;input(1);assert(view==View::Home&&draftTitle.empty());
 ready(View::Settings);input(1);assert(orientation==Orientation::PortraitInverted&&preferences.value==2);
 ready(View::Help);input(2);assert(page==1);
 ready(View::Editor);draftTitle="é";titleField=true;key=2;input(1);assert(draftTitle.empty());
 std::puts("Actual device UI controller flow tests passed (host fixtures, not hardware).");
}
'''
stage=root/'build/host';stage.mkdir(parents=True,exist_ok=True)
cpp=stage/'device_ui_flow_tests.cpp';cpp.write_text(code)
subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I'+str(root/'core'),'-I'+str(root/'firmware/device'),str(cpp),str(root/'firmware/device/raster.cpp'),str(root/'firmware/device/local_files.cpp'),str(root/'core/compagnon.cpp'),str(root/'core/note_store.cpp'),str(root/'core/note_service.cpp'),str(root/'core/task_store.cpp'),'-o',str(stage/'device_ui_flow_tests')],check=True)
subprocess.run([str(stage/'device_ui_flow_tests')],check=True)
