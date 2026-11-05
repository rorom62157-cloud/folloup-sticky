// Mon compagnon 0.2: portrait workspace, persistent notes/tasks and text reader.
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <algorithm>
#include <memory>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "board.hpp"
#include "raster.hpp"
#include "local_files.hpp"
#include "task_store.hpp"
#include "posix_note_files.hpp"
#include "embedded_http.hpp"
#include "ink.hpp"
#include "audio.hpp"
#include "clock.hpp"
#include "agenda_store.hpp"
#include "http_self_test.hpp"
#include "power_key.hpp"
#include "audio_metrics.hpp"
#include "postit_store.hpp"
#include "postit_http.hpp"
#include "deep_sleep.hpp"
#include "driver/rtc_io.h"
using namespace compagnon;
using namespace compagnon::device;
namespace {
std::unique_ptr<PosixNoteFiles> files, taskFiles, agendaFiles, recordingFiles, postitFiles;
std::unique_ptr<AgendaStore> agendaStore;
std::unique_ptr<PostitStore> postitStore;
std::unique_ptr<PostitHttp> postitHttp;
PostitStyle selectedStyle;
std::unique_ptr<NoteStore> recordingIndex;
AudioEngine audio;
AudioStatus audioState;
ClockSnapshot clockNow;
std::vector<AgendaEvent> agenda;
std::vector<AudioRecording> audios;
AgendaEvent selectedEvent;
AgendaDraft eventDraft;
AudioRecording selectedAudio;
std::string audioError, agendaError, clockError;
bool eventEditing=false, settingClock=false, editingNumber=false, powerKeyReady=false;
int numberDay=1,numberMonth=1,numberYear=2026,numberHour=12,numberMinute=0,volume=65,microphoneGain=30;
bool showBatteryPercent=false;
std::string lastAudioSaved;
int audioCategory=0;
BatterySnapshot battery;
unsigned long lastSensorPoll=0,lastAudioPaint=0;
bool holdCapture=false,backLongHandled=false,pendingPowerOff=false,pendingDeepSleep=false;
bool deepWake=false;
unsigned long wakeReleasedSince=0;
std::string lastClockMinute;
QueueHandle_t httpResults=nullptr;
bool httpTesting=false, apCredentialsStable=false;
struct HttpResult{bool ok;char summary[512];};
std::unique_ptr<NoteStore> store;
std::unique_ptr<TaskStore> taskStore;
std::unique_ptr<NoteHttp> notesApi;
std::unique_ptr<EmbeddedHttp> server;
LocalFiles browser;
Preferences preferences;
Orientation orientation = Orientation::PortraitInverted;
std::vector<Note> notes;
std::vector<Task> tasks;
std::vector<FileEntry> entries;
std::string fault, message, ssid, password, taskError, folder, fileTitle, fileText;
std::string draftTitle, draftText;
Note selected;
Task selectedTask;
enum class View { Home, Apps, Notes, Pins, Note, NoteActions, Tasks, Task, TaskActions,
    Editor, Discard, DeleteNote, DeleteTask, Files, Reader, Network, Settings, Help,
    Agenda, Event, EventActions, EventSchedule, DeleteEvent, DatePicker, TimePicker,
    Audio, AudioActions, AudioType, AudioNext, DeleteAudio, AudioSettings, DisplaySettings, ClockSettings, StorageInfo, About, PowerConfirm, PowerDown, PowerMenu, DeepScreen };
enum class Edit { NewNote, NewPin, NoteText, NoteTitle, NewTask, TaskDetails, TaskTitle, EventTitle, EventDetails };
struct SuspendedDraft {Edit kind;std::string title,text;bool titleField;View returnView;int key,mode;};
std::vector<SuspendedDraft> suspendedDrafts;
View view = View::Home, editorReturn = View::Notes;
Edit edit = Edit::NewNote;
int cursor = 0, key = 0, alphabetMode = 0;
std::size_t page = 0;
bool titleField = true, dirty = true, webActive = false, wifiEnabled = false, sdReady = false, webReady = false;
bool showingApproval = false, approvalDisplayed = false, approvalArmed = false;
std::uint64_t showingRequest = 0, uiRevision = 1, queuedConfirm = 0;
unsigned long lastPoll = 0, lastInput = 0;
uint8_t* frame = nullptr;
uint8_t* spare = nullptr;
QueueHandle_t displayJobs = nullptr, displayResults = nullptr;
bool displayBusy = false, forceFull = true;
unsigned refreshCount = 0;
struct DisplayJob { uint8_t* pixels; bool fast; std::uint64_t request; };
struct DisplayResult { bool ok; std::uint64_t request; char error[160]; };

int width() { return orientation == Orientation::Landscape ? 800 : 480; }
int height() { return orientation == Orientation::Landscape ? 480 : 800; }
int contentY() { return height() == 800 ? 168 : 120; }
int columns() { return (width() - 40) / 12; }
int visibleRows() { return std::max(1, (height() - 112 - contentY()) / 54); }
void go(View next) { queuedConfirm=0;++uiRevision;view = next; cursor = 0; page = 0; dirty = true; }
std::string shortLabel(const std::string& value, int cols = 0) {
    auto lines = wrapText(value, cols ? cols : columns());
    return lines.front() + (lines.size() > 1 ? "..." : "");
}
std::string resultMessage(NoteResult result) {
    switch (result) {
    case NoteResult::Conflict: return "Version modifiee. Brouillon conserve. Reouvrir pour comparer.";
    case NoteResult::LimitExceeded: return "Stockage plein ou limite atteinte.";
    case NoteResult::InvalidArgument: return "Titre ou texte invalide. Titre requis pour une tache.";
    case NoteResult::NotFound: return "Element supprime entre-temps.";
    case NoteResult::IoError: return "Erreur SD. Sauvegarde incertaine. Ne pas repeter la creation.";
    default: return "Stockage indisponible. Donnees conservees.";
    }
}
void displayWorker(void*) {
    DisplayJob job;
    for (;;) if (xQueueReceive(displayJobs, &job, portMAX_DELAY) == pdTRUE) {
        std::string error;
        DisplayResult result{}; result.request = job.request;
        result.ok = display(job.pixels, error, job.fast);
        snprintf(result.error, sizeof result.error, "%s", error.c_str());
        xQueueSend(displayResults, &result, portMAX_DELAY);
    }
}
struct Button {
    uint8_t pin; bool raw = false, stable = false;
    unsigned long changed = 0, repeated = 0;
    bool pressed(unsigned long now, bool repeat) {
        const bool value = digitalRead(pin) == LOW;
        if (value != raw) { raw = value; changed = now; }
        if (stable != raw && now - changed >= 30) {
            stable = raw; repeated = now; return stable;
        }
        if (repeat && stable && now - changed >= 650 && now - repeated >= 160) {
            repeated = now; return true;
        }
        return false;
    }
};
Button buttons[] = {{4}, {5}, {6}, {0}};
void refreshNotes() {
    if (!server) return;
    auto result = server->localNotes([](NoteService& service) { return service.list(); });
    if (!result.ok()) { message = result.message; return; }
    bool changed = notes.size() != result.notes.size();
    for (std::size_t i = 0; !changed && i < notes.size(); ++i)
        changed = notes[i].id != result.notes[i].id || notes[i].version != result.notes[i].version;
    if (changed) {
        notes = std::move(result.notes); dirty = true; ++uiRevision; queuedConfirm = 0;
        if (view == View::Note) {
            auto found = std::find_if(notes.begin(), notes.end(), [](const Note& n) { return n.id == selected.id; });
            if (found == notes.end()) { go(View::Notes); message = "Note supprimee depuis le web."; }
            else { selected = *found; page = 0; }
        }
    }
}
void refreshTasks() { if (taskStore && taskStore->ready()) tasks = taskStore->tasks(); else tasks.clear(); dirty = true; }
std::vector<Note> visibleNotes() {
    if (view != View::Pins) return notes;
    std::vector<Note> pins;
    for (const auto& note : notes) if (note.pinned) pins.push_back(note);
    return pins;
}
void loadFolder() {
    if (!browser.list(folder, entries, message)) entries.clear();
    else if (entries.empty()) message = "Aucun TXT/MD ici. Ajouter un fichier texte sur la SD.";
    else message = "TXT/MD en lecture seule. Clic pour ouvrir.";
    go(View::Files);
}
void loadPostitStyle(const std::string&id){
    if(!server)return;PostitStyle value;auto result=server->localPostits([&](PostitStore&styles){return styles.get(id,value);});
    if(result==NoteResult::Ok)selectedStyle=std::move(value);
    else if(result==NoteResult::NotFound||selectedStyle.note_id!=id)selectedStyle={id,PostitAlignment::Center,{},0};
}
void markActivePostit(){if(selected.pinned){preferences.putString("pin-active",selected.id.c_str());loadPostitStyle(selected.id);}}
void chooseSleepPostit(){
    std::string id=preferences.getString("pin-active","").c_str();
    auto found=std::find_if(notes.begin(),notes.end(),[&](const Note&note){return note.pinned&&note.id==id;});
    if(found==notes.end())found=std::find_if(notes.begin(),notes.end(),[](const Note&note){return note.pinned;});
    if(found!=notes.end()){selected=*found;loadPostitStyle(selected.id);}else selected={};
}
void openPowerMenu(){
    if(((view==View::Editor||view==View::Discard)&&(!draftTitle.empty()||!draftText.empty()))||!suspendedDrafts.empty()){message="Sauvegarder ou abandonner le brouillon avant la veille.";dirty=true;return;}
    go(View::PowerMenu);
}
void refreshAgenda(){if(agendaStore&&agendaStore->ready())agenda=agendaStore->events();else agenda.clear();dirty=true;}
void refreshAudios(){std::string error;if(audio.listRecordings(audios,error))dirty=true;}
bool canSuspendEditor(){if((view==View::Editor||view==View::Discard)&&suspendedDrafts.size()>=4){message="Reprendre un brouillon avant une nouvelle capture.";return false;}return true;}
void suspendEditor(){if(view==View::Editor||view==View::Discard)suspendedDrafts.push_back({edit,draftTitle,draftText,titleField,editorReturn,key,alphabetMode});}
void resumeEditor(){if(suspendedDrafts.empty())return;auto saved=suspendedDrafts.back();suspendedDrafts.pop_back();edit=saved.kind;draftTitle=std::move(saved.title);draftText=std::move(saved.text);titleField=saved.titleField;editorReturn=saved.returnView;key=saved.key;alphabetMode=saved.mode;go(View::Editor);message="Brouillon retrouvé. Audio original conservé.";}
void toggleRecording(){
    if(pendingPowerOff||pendingDeepSleep)return;
    auto status=audio.status();
    if(status.state==AudioState::Recording||status.state==AudioState::Starting){audio.stopRecording();message="Sauvegarde de l’original…";}
    else if(status.state==AudioState::Playing){audio.stopPlayback();message="Lecture arrêtée.";}
    else if(status.state==AudioState::Saving){message="Sauvegarde en cours.";}
    else if(canSuspendEditor()&&audio.startRecording(message)){suspendEditor();message="Enregistrement demandé. Attendre le signal visuel.";}
    else if(message.empty())message=audioError;
    dirty=true;
}
void saveAudioCategory(){
    if(!recordingIndex||!recordingIndex->ready()){message="Classement indisponible. L’original audio reste conservé.";return;}
    const auto cats=std::vector<std::string>{"Note","Agenda","Tâche","Rappel à traiter","Post-it","À traiter"};
    const std::string body=selectedAudio.path+"\n"+cats[std::min(audioCategory,5)];
    for(const auto&note:recordingIndex->notes())if(note.text.rfind(selectedAudio.path+"\n",0)==0){const auto id=note.id;const auto version=note.version;auto result=recordingIndex->update(id,{cats[std::min(audioCategory,5)],body,false,""},version);message=result==NoteResult::Ok?"Classement enregistré. Original conservé.":resultMessage(result);return;}
    auto result=recordingIndex->create({cats[std::min(audioCategory,5)],body,false,""});message=result==NoteResult::Ok?"Classement enregistré. Original conservé.":resultMessage(result);
}
void saveEvent(){
    if(!agendaStore||!agendaStore->ready()){message=agendaError;return;}
    AgendaDraft value=eventDraft;if(value.all_day)value.time.clear();
    AgendaEvent saved;const auto result=eventEditing?agendaStore->update(selectedEvent.id,value,selectedEvent.version,&saved):agendaStore->create(value,&saved);
    if(result==NoteResult::Ok){selectedEvent=saved;refreshAgenda();go(View::Event);message="Rendez-vous local sauvegardé.";}else message=resultMessage(result);
}
std::string numericDate(){char value[16];snprintf(value,sizeof value,"%04d-%02d-%02d",numberYear,numberMonth,numberDay);return value;}
std::string numericTime(){char value[8];snprintf(value,sizeof value,"%02d:%02d",numberHour,numberMinute);return value;}
void loadNumbers(const std::string&date,const std::string&time){
    if(validAgendaDate(date))sscanf(date.c_str(),"%d-%d-%d",&numberYear,&numberMonth,&numberDay);
    else{numberYear=2026;numberMonth=1;numberDay=1;}
    if(validAgendaTime(time))sscanf(time.c_str(),"%d:%d",&numberHour,&numberMinute);else{numberHour=12;numberMinute=0;}
    editingNumber=false;
}
bool applyClock(const std::string&date,const std::string&time){
    if(!preferences.putBool("rtc-utc",false)){message="Impossible de sécuriser la mémorisation de l’horloge.";return false;}
    bool ok=setClockLocal(date,time,message);
    if(ok&&!preferences.putBool("rtc-utc",true)){message="Heure réglée, mémorisation de provenance échouée.";ok=false;}
    clockNow=clockSnapshot();if(ok)message="Date et heure mémorisées dans l’horloge.";return ok;
}
void chooseNumber(){
    const int count=view==View::DatePicker?3:2;
    if(cursor<count){editingNumber=!editingNumber;return;}
    if(cursor==count){
        if(!validAgendaDate(numericDate())||!validAgendaTime(numericTime())){message="Date ou heure invalide.";return;}
        if(settingClock){if(applyClock(numericDate(),numericTime()))go(View::ClockSettings);}
        else{eventDraft.date=numericDate();eventDraft.time=numericTime();go(View::EventSchedule);}
    }else go(settingClock?View::ClockSettings:View::EventSchedule);
}
void adjustNumber(int event){
    if(event==1){editingNumber=false;return;}
    int*value=nullptr;int low=0,high=59;
    if(view==View::DatePicker){value=cursor==0?&numberDay:cursor==1?&numberMonth:&numberYear;low=cursor==2?2000:1;high=cursor==0?31:cursor==1?12:2099;}
    else{value=cursor==0?&numberHour:&numberMinute;high=cursor==0?23:59;}
    *value+=event==0?-1:1;if(*value<low)*value=high;if(*value>high)*value=low;
}
void httpWorker(void*){HttpResult result{};std::string summary;result.ok=runHttpSelfTest(summary);snprintf(result.summary,sizeof result.summary,"%s",summary.c_str());xQueueSend(httpResults,&result,portMAX_DELAY);vTaskDelete(nullptr);}
void startEdit(Edit kind) {
    edit = kind; editorReturn = view; titleField = true; alphabetMode = 0; key = 0;
    draftTitle.clear(); draftText.clear();
    if (kind == Edit::NoteText || kind == Edit::NoteTitle) { draftTitle = selected.title; draftText = selected.text; titleField = kind == Edit::NoteTitle; }
    if (kind == Edit::TaskDetails || kind == Edit::TaskTitle) { draftTitle = selectedTask.title; draftText = selectedTask.details; titleField = kind == Edit::TaskTitle; }
    if(kind==Edit::EventTitle){draftTitle=eventDraft.title;draftText=eventDraft.details;titleField=true;}
    if(kind==Edit::EventDetails){draftTitle=eventDraft.title;draftText=eventDraft.details;titleField=false;}
    go(View::Editor); message = "Choisir les caractères, puis valider.";
}
bool editingTask() { return edit == Edit::NewTask || edit == Edit::TaskDetails || edit == Edit::TaskTitle; }
void finishEdit() {
    if(edit==Edit::EventTitle||edit==Edit::EventDetails){
        if(draftTitle.empty()){message="Un titre est nécessaire.";return;}
        eventDraft.title=draftTitle;eventDraft.details=draftText;go(View::EventSchedule);return;
    }
    if (titleField && (edit == Edit::NewNote || edit == Edit::NewPin || edit == Edit::NewTask)) {
        if (draftTitle.empty()) { message = "Saisissez un titre."; return; }
        titleField = false; key = 0; message = "Saisissez le texte puis OK pour sauvegarder."; return;
    }
    if (editingTask()) {
        Task saved;
        const auto result = edit == Edit::NewTask ? taskStore->create(draftTitle, draftText, &saved) :
            taskStore->update(selectedTask.id, draftTitle, draftText, selectedTask.version, &saved);
        if (result != NoteResult::Ok) { message = resultMessage(result); return; }
        selectedTask = saved; refreshTasks(); go(View::Task); message = "Tache sauvegardee sur SD.";
    } else {
        auto result = server->localNotes([&](NoteService& service) {
            return (edit == Edit::NewNote || edit == Edit::NewPin) ? service.create({draftTitle, draftText, std::nullopt}) :
                service.update(selected.id, {draftTitle, draftText, selected.pinned, selected.version, std::nullopt});
        });
        if (!result.ok()) { message = result.message; return; }
        selected = *result.note;
        if (edit == Edit::NewPin) {
            auto pin = server->localNotes([&](NoteService& service) { return service.setPinned(selected.id, true, selected.version); });
            if (pin.ok()) selected = *pin.note;
            else { refreshNotes(); go(View::Note); message = "Note creee, epinglage echoue. Reessayer depuis Actions."; return; }
        }
        refreshNotes();markActivePostit();go(View::Note);message="Note sauvegardée sur SD.";
    }
    draftTitle.clear(); draftText.clear();
}
std::vector<std::string> characters() {
    const std::string source = alphabetMode == 0 ? "abcdefghijklmnopqrstuvwxyz" : alphabetMode == 1 ? "ABCDEFGHIJKLMNOPQRSTUVWXYZ" : "0123456789.,!?-:;()/@'";
    std::vector<std::string> chars;
    for (char c : source) chars.push_back(std::string(1, c));
    if (alphabetMode < 2) for (const auto* c : {"é", "è", "ê", "à", "ç", "ù", "ô", "î"}) chars.push_back(c);
    return chars;
}
std::vector<std::string> labels() {
    switch (view) {
    case View::Home: return {"Agenda","Tâches","Applications", "Enregistrer", "Post-it", "Réglages"};
    case View::Apps: return {"Agenda", "Notes", "Post-it", "Tâches", "Audio", "Fichiers", "Compagnon web", "Aide"};
    case View::Notes: case View::Pins: {
        std::vector<std::string> out{view == View::Pins ? "+ NOUVEAU POST-IT" : "+ NOUVELLE NOTE"};
        if(view==View::Notes&&!suspendedDrafts.empty())out.push_back("Reprendre le brouillon");
        for (const auto& note : visibleNotes()) out.push_back((note.pinned ? "* " : "") + (note.title.empty() ? "Sans titre" : note.title));
        return out;
    }
    case View::NoteActions: {
        std::vector<std::string> items{selected.pinned?"Désépingler":"Épingler et afficher","Modifier le texte","Renommer","Supprimer","Retour à la note"};
        if(selected.pinned)items.push_back("Alignement : "+std::string(selectedStyle.alignment==PostitAlignment::Left?"gauche":selectedStyle.alignment==PostitAlignment::Right?"droite":"centré"));return items;
    }
    case View::Tasks: {
        if (!taskStore || !taskStore->ready()) return {"Retour aux applications"};
        std::vector<std::string> out{"+ NOUVELLE TACHE"};
        for (const auto& task : tasks) out.push_back((task.done ? "[X] " : "[ ] ") + task.title);
        return out;
    }
    case View::TaskActions: return {selectedTask.done ? "Marquer a faire" : "Marquer terminee", "Modifier les details", "Renommer", "Supprimer", "Retour a la tache"};
    case View::DeleteNote: case View::DeleteTask: return {"Annuler", "Confirmer la suppression"};
    case View::Discard: return {"Reprendre le brouillon", "Abandonner le brouillon"};
    case View::Network: return {wifiEnabled ? "Couper le Wi-Fi local" : "Activer le Wi-Fi local", "Revoquer la session web", "Retour aux applications"};
    case View::Settings: return {"Réseau et compagnon web", "Audio et volume", "Date, heure et fuseau", "Écran et bandeau", "Stockage", "Aide des commandes", "À propos", "Veille et alimentation"};
    case View::DisplaySettings: return {"Orientation : " + std::string(orientation == Orientation::PortraitInverted ? "portrait" : orientation == Orientation::Portrait ? "portrait retourné" : "paysage"), "Batterie en % : " + std::string(showBatteryPercent ? "oui" : "non"), "Nettoyage complet de l'écran", "Retour aux réglages"};
    case View::AudioSettings: return {"Volume : " + std::to_string(volume) + " %", "Microphone : "+std::to_string(microphoneGain)+" dB", "Durée maximale : 4 minutes", "Retour aux réglages"};
    case View::ClockSettings: return {"Régler la date", "Régler l'heure", "Fuseau : " + (clockNow.timezone.empty() ? std::string("Europe/Madrid") : clockNow.timezone), "Retour aux réglages"};
    case View::DatePicker: return {"Jour : " + std::to_string(numberDay), "Mois : " + std::to_string(numberMonth), "Année : " + std::to_string(numberYear), "Valider la date", "Annuler"};
    case View::TimePicker: return {"Heure : " + std::to_string(numberHour), "Minute : " + std::to_string(numberMinute), "Valider l'heure", "Annuler"};
    case View::EventSchedule: return {"Date : " + eventDraft.date, "Heure : " + (eventDraft.all_day ? std::string("journée entière") : eventDraft.time), "Journée entière : " + std::string(eventDraft.all_day ? "oui" : "non"), "Modifier les détails", "Enregistrer", "Annuler"};
    case View::EventActions: return {"Modifier le titre", "Date, heure et détails", "Supprimer", "Retour à l'agenda"};
    case View::DeleteEvent: return {"Annuler", "Supprimer le rendez-vous"};
    case View::Agenda: {
        std::vector<std::string> out{"+ Nouveau rendez-vous"};
        if (agendaStore && agendaStore->ready()) for (const auto&e:agenda) out.push_back(e.date.substr(5)+" · "+(e.all_day ? "Journée" : e.time)+" · "+e.title);
        return out;
    }
    case View::Audio: {
        if(audioState.state==AudioState::Recording||audioState.state==AudioState::Starting||audioState.state==AudioState::Saving||audioState.state==AudioState::Playing)return {audioState.state==AudioState::Saving?"Sauvegarde en cours":"Arrêter"};
        std::vector<std::string> out{audioState.state==AudioState::Recording || audioState.state==AudioState::Starting ? "Arrêter et sauvegarder" : audioState.state==AudioState::Playing ? "Arrêter la lecture" : "+ Enregistrer"};
        for(size_t i=0;i<audios.size();++i){std::string label="Audio "+std::to_string(i+1);if(audios[i].modifiedEpoch){std::time_t epoch=audios[i].modifiedEpoch;std::tm local{};localtime_r(&epoch,&local);char date[24];strftime(date,sizeof date,"%d/%m %H:%M",&local);label=date;}out.push_back(label+" · "+std::to_string(audios[i].durationMs/1000)+" s");}
        return out;
    }
    case View::AudioActions: return {"Écouter", "Classer cet audio", "Retour aux audios", "Supprimer l’audio"};
    case View::DeleteAudio:return {"Annuler","Supprimer définitivement"};
    case View::PowerConfirm:return {"Annuler","Confirmer l’extinction"};
    case View::PowerMenu:return {"Veille prolongée", "Éteindre l’appareil", "Annuler"};
    case View::AudioType: return {"Note", "Agenda", "Tâche", "Rappel à traiter", "Post-it", "Conserver à traiter", "Annuler"};
    case View::AudioNext: return {"Conserver l'original", "Écouter", audioCategory==1 ? "Préparer un rendez-vous" : audioCategory==2 ? "Créer une tâche liée" : audioCategory==4 ? "Créer un post-it lié" : "Retour au classement", "Retour aux audios"};
    case View::Files: {
        std::vector<std::string> out{folder.empty() ? "Retour aux applications" : ".. Dossier parent"};
        for (const auto& entry : entries) out.push_back((entry.directory ? "+ " : "") + entry.name);
        return out;
    }
    default: return {};
    }
}
void back() {
    if (view == View::Editor) { go(View::Discard); return; }
    if (view == View::Discard) { go(View::Editor); return; }
    if(view==View::Home)return;
    if((view==View::Audio||view==View::AudioType||view==View::AudioNext)&&!suspendedDrafts.empty()){resumeEditor();return;}
    if(view==View::Settings){go(View::Home);return;}
    if(view==View::PowerConfirm||view==View::PowerMenu){go(View::Home);return;}
    if(view==View::DisplaySettings||view==View::AudioSettings||view==View::ClockSettings||view==View::StorageInfo||view==View::About){go(View::Settings);return;}
    if(view==View::DatePicker||view==View::TimePicker){editingNumber=false;go(settingClock?View::ClockSettings:View::EventSchedule);return;}
    if(view==View::EventActions||view==View::DeleteEvent){go(View::Event);return;}
    if(view==View::Event||view==View::EventSchedule){go(View::Agenda);return;}
    if(view==View::AudioActions||view==View::AudioType||view==View::AudioNext||view==View::DeleteAudio){go(View::Audio);return;}
    if (view == View::NoteActions || view == View::DeleteNote) { go(View::Note); return; }
    if (view == View::TaskActions || view == View::DeleteTask) { go(View::Task); return; }
    if (view == View::Note) { go(View::Notes); return; }
    if (view == View::Task) { go(View::Tasks); return; }
    if (view == View::Reader) { go(View::Files); return; }
    if (view == View::Files && !folder.empty()) { const auto pos = folder.rfind('/'); folder = pos == std::string::npos ? "" : folder.substr(0, pos); loadFolder(); return; }
    if (view == View::Apps) go(View::Home); else go(View::Apps);
}
void toggleWifi() {
    if (wifiEnabled) { server->revoke(); WiFi.softAPdisconnect(true); wifiEnabled = false; message = "Wi-Fi local coupe. Session revoquee."; }
    else {
        WiFi.mode(WIFI_AP); wifiEnabled = apCredentialsStable && WiFi.softAP(ssid.c_str(), password.c_str(), 1, false, 1);
        if (wifiEnabled && !webReady) webReady = server->begin() == ESP_OK;
        message = wifiEnabled && webReady ? "Compagnon web disponible." : "Reseau indisponible. Fonctions locales actives.";
    }
    dirty = true;
}
void choose() {
    switch (view) {
    case View::Home:
        if(cursor==0)go(View::Agenda);else if(cursor==1)go(View::Tasks);else if(cursor==2)go(View::Apps);else if(cursor==3){toggleRecording();go(View::Audio);}else if(cursor==4)go(View::Pins);else go(View::Settings);break;
    case View::Apps: {
        const View pages[] = {View::Agenda, View::Notes, View::Pins, View::Tasks, View::Audio, View::Files, View::Network, View::Help};
        const auto next = pages[cursor]; if (next == View::Files) { folder.clear(); loadFolder(); } else go(next); break;
    }
    case View::Notes: case View::Pins: {
        const bool pins = view == View::Pins;
        const auto available = visibleNotes();const bool resume=view==View::Notes&&!suspendedDrafts.empty();const int index=cursor-1-(resume?1:0);
        if(cursor==0)startEdit(pins?Edit::NewPin:Edit::NewNote);
        else if(resume&&cursor==1)resumeEditor();
        else if(index>=0&&static_cast<size_t>(index)<available.size()){selected=available[index];markActivePostit();go(View::Note);}break;
    }
    case View::Note: go(View::NoteActions); break;
    case View::NoteActions:
        if (cursor == 0) {
            auto result = server->localNotes([&](NoteService& service) { return service.setPinned(selected.id, !selected.pinned, selected.version); });
            if (result.ok()) { selected = *result.note; refreshNotes();markActivePostit();go(View::Note); message = selected.pinned ? "Post-it affiche." : "Post-it desepingle."; }
            else message = result.message;
        } else if (cursor == 1) startEdit(Edit::NoteText); else if (cursor == 2) startEdit(Edit::NoteTitle);
        else if(cursor==3)go(View::DeleteNote);
        else if(cursor==5&&selected.pinned){
            loadPostitStyle(selected.id);auto alignment=selectedStyle.alignment==PostitAlignment::Left?PostitAlignment::Center:selectedStyle.alignment==PostitAlignment::Center?PostitAlignment::Right:PostitAlignment::Left;
            auto result=server->localPostits([&](PostitStore&styles){return styles.put(selected.id,alignment,selectedStyle.strokes,selectedStyle.version,&selectedStyle);});
            if(result==NoteResult::Ok)message="Alignement mémorisé.";else message=resultMessage(result);
        }else go(View::Note);break;
    case View::Tasks:
        if (!taskStore || !taskStore->ready()) go(View::Apps);
        else if (cursor == 0) startEdit(Edit::NewTask);
        else if (static_cast<std::size_t>(cursor - 1) < tasks.size()) { selectedTask = tasks[cursor - 1]; go(View::Task); } break;
    case View::Task: go(View::TaskActions); break;
    case View::TaskActions:
        if (cursor == 0) {
            Task saved; const auto result = taskStore->setDone(selectedTask.id, !selectedTask.done, selectedTask.version, &saved);
            if (result == NoteResult::Ok) { selectedTask = saved; refreshTasks(); go(View::Task); message = saved.done ? "Tache terminee." : "Tache a faire."; }
            else message = resultMessage(result);
        } else if (cursor == 1) startEdit(Edit::TaskDetails); else if (cursor == 2) startEdit(Edit::TaskTitle);
        else if (cursor == 3) go(View::DeleteTask); else go(View::Task); break;
    case View::DeleteNote:
        if (cursor == 0) go(View::Note); else {
            auto result = server->localNotes([&](NoteService& service) { return service.erase(selected.id, selected.version); });
            if(result.ok()){server->localPostits([&](PostitStore&styles){return styles.eraseForNote(selected.id);});refreshNotes();go(View::Notes);message="Note supprimée.";} else message = result.message;
        } break;
    case View::DeleteTask:
        if (cursor == 0) go(View::Task); else {
            const auto result = taskStore->erase(selectedTask.id, selectedTask.version);
            if (result == NoteResult::Ok) { refreshTasks(); go(View::Tasks); message = "Tache supprimee."; } else message = resultMessage(result);
        } break;
    case View::Discard:
        if (cursor == 0) go(View::Editor); else { draftTitle.clear(); draftText.clear(); go(editorReturn); message = "Brouillon abandonne."; } break;
    case View::Files:
        if (cursor == 0) back(); else if (static_cast<std::size_t>(cursor - 1) < entries.size()) {
            const auto entry = entries[cursor - 1];
            if (entry.directory) { folder = entry.relative_path; loadFolder(); }
            else if (browser.readText(entry.relative_path, fileText, message)) { fileTitle = entry.name; go(View::Reader); }
        } break;
    case View::Network:
        if (cursor == 0) toggleWifi(); else if (cursor == 1) { server->revoke(); message = "Acces web revoque."; } else go(View::Apps); break;
    case View::Settings: {
        if(cursor==7){openPowerMenu();break;}
        const View pages[]={View::Network,View::AudioSettings,View::ClockSettings,View::DisplaySettings,View::StorageInfo,View::Help,View::About,View::PowerMenu};go(pages[cursor]);break;
    }
    case View::DisplaySettings:
        if (cursor == 0) {
            orientation = orientation == Orientation::Portrait ? Orientation::PortraitInverted : orientation == Orientation::PortraitInverted ? Orientation::Landscape : Orientation::Portrait;
            if (preferences.putUChar("orientation", static_cast<uint8_t>(orientation)) != 1) message = "Orientation active, memorisation echouee.";
            else message = "Orientation memorisee.";
            forceFull = true;
        } else if(cursor==1){showBatteryPercent=!showBatteryPercent;if(preferences.putBool("battery-pct",showBatteryPercent))message=battery.available?"Bandeau mémorisé.":"Bandeau mémorisé. Mesure indisponible.";else message="Bandeau appliqué. Mémorisation échouée.";}
        else if(cursor==2){forceFull=true;message="Nettoyage complet demandé.";}else go(View::Settings);break;
    case View::StorageInfo: go(View::Settings); break;
    case View::AudioSettings:
        if(cursor==0){const int next=volume>=100?0:volume+10; if(audio.setVolume(next,message)){volume=next;message=preferences.putUChar("volume",volume)==1?"Volume mémorisé.":"Volume appliqué. Mémorisation échouée.";}}
        else if(cursor==1){int next=microphoneGain>=42?0:microphoneGain+6;if(audio.setMicrophoneGain(next,message)){microphoneGain=next;message=preferences.putUChar("mic-gain",next)==1?"Gain microphone mémorisé.":"Gain appliqué, mémorisation échouée.";}}
        else if(cursor==3)go(View::Settings);else message="Arrêt automatique après quatre minutes.";break;
    case View::ClockSettings:
        if(cursor<2){settingClock=true;loadNumbers(clockNow.date,clockNow.time);go(cursor==0?View::DatePicker:View::TimePicker);}
        else if(cursor==2){const std::string zone=clockNow.timezone=="UTC"?"Europe/Madrid":"UTC";if(configureClockTimezone(zone)){preferences.putString("timezone",zone.c_str());clockNow=clockSnapshot();message=std::string(preferences.getString("timezone","").c_str())==zone?"Fuseau mémorisé.":"Fuseau appliqué. Mémorisation échouée.";}}
        else go(View::Settings);break;
    case View::DatePicker:case View::TimePicker: chooseNumber();break;
    case View::Agenda:
        if(!agendaStore||!agendaStore->ready()){message=agendaError;break;}
        if(cursor==0){eventEditing=false;eventDraft={"","",clockNow.valid?clockNow.date:"2000-01-01",clockNow.valid?clockNow.time:"12:00",false};startEdit(Edit::EventTitle);}
        else if(static_cast<size_t>(cursor-1)<agenda.size()){selectedEvent=agenda[cursor-1];go(View::Event);}break;
    case View::Event:go(View::EventActions);break;
    case View::EventActions:
        eventEditing=true;eventDraft={selectedEvent.title,selectedEvent.details,selectedEvent.date,selectedEvent.time,selectedEvent.all_day};
        if(cursor==0)startEdit(Edit::EventTitle);else if(cursor==1)go(View::EventSchedule);else if(cursor==2)go(View::DeleteEvent);else go(View::Agenda);break;
    case View::EventSchedule:
        if(cursor<2){settingClock=false;loadNumbers(eventDraft.date,eventDraft.time);go(cursor==0?View::DatePicker:View::TimePicker);}
        else if(cursor==2){eventDraft.all_day=!eventDraft.all_day;if(!eventDraft.all_day&&eventDraft.time.empty())eventDraft.time="12:00";}
        else if(cursor==3)startEdit(Edit::EventDetails);
        else if(cursor==4)saveEvent();else go(View::Agenda);break;
    case View::DeleteEvent:
        if(cursor==0)go(View::Event);else {const auto result=agendaStore->erase(selectedEvent.id,selectedEvent.version);if(result==NoteResult::Ok){refreshAgenda();go(View::Agenda);message="Rendez-vous supprimé.";}else message=resultMessage(result);}break;
    case View::Audio:
        if(cursor==0)toggleRecording();else if(static_cast<size_t>(cursor-1)<audios.size()){selectedAudio=audios[cursor-1];go(View::AudioActions);}break;
    case View::AudioActions:
        if(cursor==0){if(audio.play(selectedAudio.path,message))go(View::Audio);}
        else if(cursor==1)go(View::AudioType);else if(cursor==3)go(View::DeleteAudio);else go(View::Audio);break;
    case View::DeleteAudio:
        if(cursor==0)go(View::AudioActions);else if(audio.eraseRecording(selectedAudio.path,message)){
            if(recordingIndex&&recordingIndex->ready())for(const auto&note:recordingIndex->notes())if(note.text.rfind(selectedAudio.path+"\n",0)==0){const auto id=note.id;const auto version=note.version;recordingIndex->erase(id,version);break;}
            refreshAudios();go(View::Audio);message="Audio supprimé sur confirmation.";
        }break;
    case View::AudioType:if(cursor==6){if(!suspendedDrafts.empty())resumeEditor();else go(View::Audio);message="Classement annulé. Audio original conservé.";}else{audioCategory=cursor;saveAudioCategory();go(View::AudioNext);}break;
    case View::AudioNext:
        if(cursor==0&&!suspendedDrafts.empty())resumeEditor();else if(cursor==0||cursor==3)go(View::Audio);
        else if(cursor==1){if(audio.play(selectedAudio.path,message))go(View::Audio);}
        else if(audioCategory==1){eventEditing=false;eventDraft={"", "Audio local : "+selectedAudio.path,clockNow.valid?clockNow.date:"2000-01-01","12:00",false};startEdit(Edit::EventTitle);}
        else if(audioCategory==2){startEdit(Edit::NewTask);draftText="Audio local : "+selectedAudio.path;}
        else if(audioCategory==4){startEdit(Edit::NewPin);draftText="Audio local : "+selectedAudio.path;}
        else go(View::AudioType);break;
    case View::Help:go(View::Home);break;
    case View::PowerMenu:
        if(cursor==0){
            auto state=audio.status();
            if(state.state==AudioState::Recording||state.state==AudioState::Starting||state.state==AudioState::Saving||state.state==AudioState::Playing){message="Terminer et sauvegarder l’audio avant la veille.";break;}
            if(!suspendedDrafts.empty()){message="Reprendre ou abandonner le brouillon avant la veille.";break;}
            chooseSleepPostit();wakeReleasedSince=0;pendingDeepSleep=true;go(View::DeepScreen);message="";
        }else if(cursor==1)go(View::PowerConfirm);else go(View::Home);break;
    case View::PowerConfirm:
        if(cursor==0)go(View::Settings);else if(audioState.state==AudioState::Recording||audioState.state==AudioState::Saving||audioState.state==AudioState::Starting||audioState.state==AudioState::Playing)message="Arrêter et sauvegarder l’audio avant d’éteindre.";
        else{pendingPowerOff=true;go(View::PowerDown);message="";}break;
    case View::About:go(View::Settings);break;
    default: break;
    }
}
void input(int event) {
    if(pendingPowerOff||pendingDeepSleep)return;
    lastInput = millis();
    if (showingApproval) {
        dirty = true;
        if (!approvalDisplayed || !approvalArmed) return;
        if (event == 1 || event == 3) {
            approvalArmed = false;
            const auto result = server->physicalDecision(event == 1, showingRequest);
            message = result == Result::Ok ? (event == 1 ? "Acces web autorise." : "Acces web refuse.") : "Demande expiree ou occupee.";
        }
        return;
    }
    if (!fault.empty()) return;
    // Coalesce navigation immediately, but execute confirmation only after the
    // matching model has actually reached the e-paper. Later navigation cancels it.
    if (event == 1 && (displayBusy || dirty)) { queuedConfirm = uiRevision; return; }
    queuedConfirm = 0; ++uiRevision; dirty = true;
    if(event==3){editingNumber=false;back();return;}
    if((view==View::DatePicker||view==View::TimePicker)&&editingNumber){adjustNumber(event);return;}
    if (view == View::Editor) {
        const auto chars = characters(); const int count = 6 + chars.size();
        if (event != 1) { key = (key + count + (event == 0 ? -1 : 1)) % count; return; }
        auto& value = titleField ? draftTitle : draftText;
        if (key == 0) finishEdit();
        else if (key == 4) go(View::Discard);
        else if (key == 5) { alphabetMode = (alphabetMode + 1) % 3; key = 6; }
        else if (key == 2) { if (!value.empty()) { auto pos = value.size() - 1; while (pos && (static_cast<unsigned char>(value[pos]) & 0xc0) == 0x80) --pos; value.resize(pos); } }
        else {
            const auto character = key == 1 ? std::string(" ") : key == 3 ? std::string(titleField ? " " : "\n") : chars[key - 6];
            const auto limit = titleField ? NoteStore::max_title_bytes : NoteStore::max_text_bytes;
            if (value.size() + character.size() <= limit) value += character; else message = "Limite du champ atteinte.";
        }
        return;
    }
    if (event == 1) { choose(); return; }
    if (view == View::Note || view == View::Task || view == View::Reader || view == View::Help || view == View::Event) {
        if (event == 0 && page) --page;
        else if (event == 2) ++page; // Clamped to actual pages at render.
        return;
    }
    const auto items = labels();
    if (!items.empty()) cursor = (cursor + items.size() + (event == 0 ? -1 : 1)) % items.size();
}
std::string viewTitle() {
    switch (view) {
    case View::Home: return "Aujourd’hui"; case View::Apps: return "Applications";
    case View::Notes: return "Mes notes"; case View::Pins: return "Mes post-it";
    case View::Note: return selected.pinned ? "Post-it" : "Note"; case View::NoteActions: return "Actions note";
    case View::Tasks: return "Mes taches"; case View::Task: return "Tache"; case View::TaskActions: return "Actions tache";
    case View::Files: return "Fichiers texte"; case View::Reader: return "Lecture";
    case View::Network: return "Compagnon web"; case View::Settings: return "Réglages";
    case View::Help: return "Aide"; case View::Editor: return titleField ? "Saisir le titre" : "Saisir le texte";
    case View::Discard:return "Brouillon";case View::Agenda:return "Agenda";case View::Event:return "Rendez-vous";case View::EventActions:return "Actions";case View::EventSchedule:return "Quand ?";
    case View::DatePicker:return "Choisir la date";case View::TimePicker:return "Choisir l’heure";
    case View::Audio:return "Notes vocales";case View::AudioActions:return "Cet enregistrement";case View::AudioType:return "Classer l’audio";case View::AudioNext:return "Quelle action ?";
    case View::AudioSettings:return "Audio";case View::DisplaySettings:return "Écran et bandeau";case View::ClockSettings:return "Date et heure";case View::StorageInfo:return "Stockage";case View::About:return "Mon compagnon";case View::PowerConfirm:return "Éteindre ?";case View::PowerDown:return "À bientôt";case View::PowerMenu:return "Veille et alimentation";case View::DeepScreen:return "Veille prolongée";default:return "Supprimer ?";
    }
}
Icon menuIcon(const std::string&label){
    if(label.find("Veille")!=std::string::npos)return Icon::Moon;
    if(label.find("Agenda")!=std::string::npos||label.find("rendez-vous")!=std::string::npos)return Icon::Calendar;
    if(label.find("Audio")!=std::string::npos||label.find("Enregistrer")!=std::string::npos)return Icon::Mic;
    if(label.find("Post")!=std::string::npos||label.find("post")!=std::string::npos)return Icon::Pin;
    if(label.find("Tâche")!=std::string::npos||label.find("Tache")!=std::string::npos)return Icon::Task;
    if(label.find("Fichier")!=std::string::npos)return Icon::Folder;
    if(label.find("glage")!=std::string::npos||label.find("cran")!=std::string::npos||label.find("Orientation")!=std::string::npos)return Icon::Gear;
    if(label.find("Compagnon")!=std::string::npos||label.find("seau")!=std::string::npos)return Icon::Wifi;
    if(label.find("Aide")!=std::string::npos)return Icon::Help;
    if(label.find("pplication")!=std::string::npos)return Icon::Grid;
    if(label.find("+")==0)return Icon::Plus;
    return Icon::Note;
}
void listDraw(Raster&raster,int startY=-1){
    Ink ink(raster);const auto items=labels();if(items.empty())return;
    cursor=std::max(0,std::min(cursor,int(items.size())-1));
    const int top=startY<0?176:startY;
    const int row=height()==800?64:48;
    const int visible=std::max(1,(height()-122-top)/row),start=(cursor/visible)*visible;
    for(int i=start;i<int(items.size())&&i<start+visible;++i){int y=top+(i-start)*row;bool active=cursor==i;
        ink.box(20,y,width()-40,row-8,12,active);
        ink.icon(menuIcon(items[i]),34,y+13,26,active);
        ink.text(76,y+13,ink.fit(items[i],width()-110,Face::Body),Face::Body,active);
    }
    ink.text(width()-90,height()-116,std::to_string(cursor+1)+" / "+std::to_string(items.size()),Face::Small);
}
void readerDraw(Raster&raster,const std::string&title,const std::string&body){
    Ink ink(raster);const auto lines=ink.wrap(title+"\n\n"+(body.empty()?"Sans texte":body),width()-52,Face::Body);
    const size_t perPage=std::max(1,(height()-300)/30),count=std::max<size_t>(1,(lines.size()+perPage-1)/perPage);page=std::min(page,count-1);
    for(size_t i=page*perPage;i<lines.size()&&i<(page+1)*perPage;++i)ink.text(26,174+(i-page*perPage)*30,lines[i],Face::Body);
    ink.text(26,height()-120,"Page "+std::to_string(page+1)+" / "+std::to_string(count),Face::Small);
}
void editorDraw(Raster&raster){
    Ink ink(raster);const auto&value=titleField?draftTitle:draftText;
    ink.text(26,154,std::to_string(value.size())+(titleField?" / 160 octets":" / 4096 octets")+" · "+(alphabetMode==0?"abc":alphabetMode==1?"ABC":"123"),Face::Small);
    const auto chars=characters();const int cols=std::min(18,(width()-44)/42),rows=(chars.size()+cols-1)/cols;
    const int keysY=height()-(height()==800?130:112)-rows*36,commandsY=keysY-88,previewY=height()==800?196:182;
    const int previewRows=std::max(1,(commandsY-previewY-12)/28);
    auto lines=ink.wrap(value+"|",width()-52,Face::Body);size_t start=lines.size()>size_t(previewRows)?lines.size()-previewRows:0;
    for(size_t i=start;i<lines.size();++i)ink.text(26,previewY+(i-start)*28,lines[i],Face::Body);
    const std::string commands[]={titleField&&(edit==Edit::NewNote||edit==Edit::NewPin||edit==Edit::NewTask)?"Suivant":"Sauver","Espace","Effacer","Ligne","Annuler",alphabetMode==0?"ABC":alphabetMode==1?"123":"abc"};
    const int cell=(width()-40)/3;
    for(int i=0;i<6;++i){int x=22+(i%3)*cell,y=commandsY+(i/3)*40;ink.box(x,y,cell-8,34,8,key==i);ink.text(x+8,y+5,commands[i],Face::Small,key==i);}
    for(size_t i=0;i<chars.size();++i){int x=24+(i%cols)*42,y=keysY+(i/cols)*36;bool active=key==int(i)+6;ink.box(x-2,y-2,34,32,8,active);ink.text(x+6,y,chars[i],Face::Body,active);}
}
void postitDraw(Raster&raster,const Note&note,bool sleeping=false){
    Ink ink(raster);const int x=26,w=width()-52,top=76,h=height()-200;
    PostitStyle style=selectedStyle.note_id==note.id?selectedStyle:PostitStyle{note.id,PostitAlignment::Center,{},0};
    const bool drawing=!style.strokes.empty();const int drawH=drawing?std::min(w/2,h/3):0,drawW=drawH*2;
    struct Pair{Face title,body;};
    const Pair choices[]={{Face::Hero,Face::Display},{Face::HeadingLarge,Face::Large},{Face::TitleLarge,Face::Lead},{Face::Title,Face::Reading},{Face::Heading,Face::Body},{Face::Strong,Face::Small}};
    Pair chosen=choices[5];std::vector<std::string> titleLines,bodyLines;int titleHeight=0;
    for(const auto&pair:choices){
        auto titles=note.title.empty()?std::vector<std::string>{}:ink.wrap(note.title,w,pair.title);
        auto bodies=note.text.empty()?std::vector<std::string>{}:ink.wrap(note.text,w,pair.body);
        const bool singleTitle=note.title.find_first_of(" \n\t")==std::string::npos,singleBody=note.text.find_first_of(" \n\t")==std::string::npos;
        if((singleTitle&&titles.size()>1)||(singleBody&&bodies.size()>1)){if(pair.title!=Face::Strong)continue;}
        const int th=titles.size()*ink.lineHeight(pair.title),bh=bodies.size()*ink.lineHeight(pair.body);
        chosen=pair;titleLines=std::move(titles);bodyLines=std::move(bodies);titleHeight=th;
        if(th+bh+(th&&bh?18:0)+(drawing?drawH+22:0)<=h)break;
    }
    const int gap=titleHeight&&!bodyLines.empty()?18:0;
    const int bodySpace=std::max(ink.lineHeight(chosen.body),h-titleHeight-gap-(drawing?drawH+22:0));
    const size_t perPage=std::max(1,bodySpace/ink.lineHeight(chosen.body));const size_t pages=std::max<size_t>(1,(bodyLines.size()+perPage-1)/perPage);page=std::min(page,pages-1);
    const auto begin=std::min(page*perPage,bodyLines.size()),end=std::min(begin+perPage,bodyLines.size());
    const int visibleBody=(end-begin)*ink.lineHeight(chosen.body),total=titleHeight+gap+visibleBody+(drawing?drawH+22:0);
    int y=top+std::max(0,(h-total)/2);
    auto drawLine=[&](const std::string&line,Face face){const int used=ink.measure(line,face);int left=x;if(style.alignment==PostitAlignment::Center)left+=(w-used)/2;else if(style.alignment==PostitAlignment::Right)left+=w-used;ink.text(left,y,line,face);y+=ink.lineHeight(face);};
    for(const auto&line:titleLines)drawLine(line,chosen.title);y+=gap;
    for(size_t i=begin;i<end;++i)drawLine(bodyLines[i],chosen.body);
    if(drawing){const int drawX=x+(w-drawW)/2,drawY=y+22;
        for(const auto&stroke:style.strokes){for(size_t i=0;i<stroke.size();++i){const int px=drawX+stroke[i].x*drawW/1023,py=drawY+stroke[i].y*drawH/1023;if(i==0)raster.bar(px-1,py-1,3,3);else{const int oldX=drawX+stroke[i-1].x*drawW/1023,oldY=drawY+stroke[i-1].y*drawH/1023;ink.line(oldX,oldY,px,py,true,3);}}}
    }
    if(pages>1&&!sleeping)ink.text(26,height()-104,std::to_string(page+1)+" / "+std::to_string(pages)+" · Haut / bas pour lire",Face::Small);
    if(sleeping)ink.text(26,height()-48,"Veille prolongée · clic molette pour réveiller",Face::Small);
}
void statusDraw(Raster&raster){
    Ink ink(raster);ink.text(22,10,clockNow.valid?clockNow.time:"--:--",Face::Heading);ink.text(114,15,"Local",Face::Small);
    const int y=4,size=44;
    ink.icon(Icon::Wifi,width()-118,y,size);if(!wifiEnabled)ink.line(width()-118,y+4,width()-74,y+40);
    if(webActive)ink.icon(Icon::Lock,width()-174,y,size);
    if(audioState.state==AudioState::Recording||audioState.state==AudioState::Saving)ink.icon(Icon::Mic,width()-230,y,size);
    else if(audioState.state==AudioState::Playing)ink.icon(Icon::Play,width()-230,y,size);
    ink.icon(Icon::Battery,width()-62,y,size);
    if(battery.available&&battery.percent>=0){if(showBatteryPercent)ink.text(width()-57,17,std::to_string(battery.percent),Face::Small);else raster.bar(width()-54,22,std::max(0,battery.percent*24/100),9);}
    else ink.text(width()-45,17,"?",Face::Small);
    ink.line(22,52,width()-22,52,true,1);
}
std::string civilLabel(const std::string&date){
    if(date.size()!=10)return "Date et heure à régler";
    const char*months[]={"janvier","février","mars","avril","mai","juin","juillet","août","septembre","octobre","novembre","décembre"};
    int y=0,m=0,d=0;sscanf(date.c_str(),"%d-%d-%d",&y,&m,&d);if(m<1||m>12)return date;
    return std::to_string(d)+" "+months[m-1]+" "+std::to_string(y);
}
void todayDraw(Raster&raster){
    Ink ink(raster);if(height()!=800){ink.text(24,75,"Aujourd’hui",Face::Title);listDraw(raster,164);return;}ink.text(24,75,"Aujourd’hui",Face::Title);ink.text(26,127,civilLabel(clockNow.date),Face::Body);
    ink.text(24,166,clockNow.valid?clockNow.time:"--:--",Face::Clock);
    ink.box(width()-112,172,88,66,18,false);ink.icon(Icon::Today,width()-90,185,40);
    auto card=[&](int y,const std::string&eyebrow,const std::string&title,const std::string&sub,Icon icon,int index){bool active=cursor==index;ink.box(24,y,width()-48,106,16,active);ink.icon(icon,42,y+20,32,active);ink.text(90,y+12,eyebrow,Face::Small,active);ink.text(42,y+46,ink.fit(title,width()-90,Face::Heading),Face::Heading,active);if(!sub.empty())ink.text(42,y+78,ink.fit(sub,width()-90,Face::Small),Face::Small,active);};
    std::vector<AgendaEvent> next;if(clockNow.valid&&agendaStore&&agendaStore->ready())next=agendaStore->upcoming(clockNow.date,clockNow.time,1);
    card(262,"Votre agenda",next.empty()?"Rien de prévu":next.front().title,next.empty()?"Ajouter un rendez-vous local":(next.front().all_day?"Journée entière":next.front().date+" · "+next.front().time),Icon::Calendar,0);
    const auto pending=std::count_if(tasks.begin(),tasks.end(),[](const Task&t){return !t.done;});std::string taskTitle="Tout est à jour";for(const auto&t:tasks)if(!t.done){taskTitle=t.title;break;}
    card(384,std::to_string(pending)+" tâche(s) à faire",taskTitle,std::to_string(audios.size())+" audio(s) · "+std::to_string(notes.size())+" note(s)",Icon::Task,1);
    const std::string names[]={"Applications","Enregistrer","Post-it","Réglages"};const Icon icons[]={Icon::Grid,Icon::Mic,Icon::Pin,Icon::Gear};int cell=(width()-64)/2;
    for(int i=0;i<4;++i){int x=24+(i%2)*(cell+16),y=512+(i/2)*88;bool active=cursor==i+2;ink.box(x,y,cell,74,14,active);ink.icon(icons[i],x+14,y+18,30,active);ink.text(x+56,y+23,ink.fit(names[i],cell-68,Face::Body),Face::Body,active);}
}
void appsDraw(Raster&raster){
    Ink ink(raster);auto items=labels();cursor=std::max(0,std::min(cursor,int(items.size())-1));
    const int cols=height()==800?2:4;int cell=(width()-48-(cols-1)*16)/cols;for(size_t i=0;i<items.size();++i){int x=24+(i%cols)*(cell+16),y=174+(i/cols)*118;bool active=cursor==int(i);ink.box(x,y,cell,104,16,active);ink.icon(menuIcon(items[i]),x+18,y+16,34,active);ink.text(x+18,y+62,ink.fit(items[i],cell-32,Face::Body),Face::Body,active);}
}
void helpDraw(Raster&raster){
    Ink ink(raster);
    if(page||height()!=800){const size_t offset=height()==800?1:0;page=page>=offset?page-offset:0;readerDraw(raster,"Les gestes utiles","Maintenir le bouton haut droit pour enregistrer. Relâcher pour conserver l’original puis le classer.\n\nLa molette déplace le choix, son clic valide. Le bouton bas droit revient. Un appui long ouvre Veille et alimentation. Choisir Veille prolongée puis réveiller avec un clic sur la molette.\n\nUn appui de validation attend que le choix soit visible. Réglages est accessible depuis Aujourd’hui.\n\nLe Wi-Fi et sa clé sont permanents. La connexion web est provisoirement directe sur le réseau protégé.\n\nGoogle et Gemini viendront ensuite.");page+=offset;return;}
    ink.text(26,150,"Les boutons, en un coup d’œil.",Face::Body);
    ink.box(168,210,142,280,22,false);ink.box(181,228,116,224,7,false);
    ink.text(191,246,"Aujourd’hui",Face::Small);ink.line(192,280,284,280,true,1);ink.box(192,294,92,48,6,false);ink.box(192,355,92,48,6,false);
    ink.box(218,480,42,12,4,false);ink.text(210,506,"USB-C",Face::Small);
    ink.box(157,270,10,88,4,true);ink.circle(162,314,5,false,2);
    ink.line(42,280,150,280,true,1);ink.text(24,239,"Naviguer",Face::Small);ink.text(24,263,"Haut / bas",Face::Small);
    ink.line(42,354,150,324,true,1);ink.text(24,366,"Valider",Face::Small);ink.text(24,390,"Clic central",Face::Small);
    ink.box(311,258,10,28,4,true);ink.line(326,270,442,270,true,1);ink.text(330,218,"Dicter",Face::Body);ink.text(330,243,"Maintenir",Face::Small);
    ink.box(311,352,10,28,4,true);ink.line(326,364,442,364,true,1);ink.text(330,383,"Retour",Face::Body);ink.text(330,411,"Appui court",Face::Small);
    ink.box(24,558,width()-48,92,16,false);ink.icon(Icon::Mic,42,580,34);ink.text(92,572,"L’original d’abord.",Face::Heading);ink.text(92,610,"L’audio est conservé avant classement.",Face::Small);
    ink.text(26,674,"Bas pour continuer l’aide",Face::Small);
}
void render(){
    if(!frame||displayBusy)return;Raster raster(frame,orientation);raster.clear();Ink ink(raster);statusDraw(raster);
    if(view==View::DeepScreen){raster.clear();if(selected.pinned&&!selected.id.empty())postitDraw(raster,selected,true);else{ink.icon(Icon::Moon,width()/2-34,height()/2-124,68);ink.text(70,height()/2-20,"Mon compagnon",Face::Title);ink.text(110,height()/2+40,"Veille prolongée",Face::Body);ink.text(42,height()-90,"Clic sur la molette pour réveiller",Face::Body);}}
    else if(view==View::PowerDown){raster.clear();ink.icon(Icon::Note,width()/2-32,height()/2-90,64);ink.text(92,height()/2,"À bientôt.",Face::Title);ink.text(72,height()/2+62,"Vos données restent sur la SD.",Face::Body);}
    else if(view==View::Home&&!showingApproval&&fault.empty())todayDraw(raster);
    else{
        ink.text(24,78,viewTitle(),Face::Title);ink.text(26,128,view==View::Settings?"À portée de main depuis Aujourd’hui":view==View::Apps?"Vos outils, simplement.":view==View::Editor?"Un titre, un texte, une idée.":"Mon compagnon · local et personnel",Face::Small);
        if(!fault.empty())readerDraw(raster,"Une erreur est survenue",fault+"\nLes originaux restent conservés.");
        else if(view==View::Apps)appsDraw(raster);
        else if(view==View::Help)helpDraw(raster);
        else if(view==View::Note){if(selected.pinned){raster.bar(0,55,width(),height()-55,false);postitDraw(raster,selected);}else readerDraw(raster,selected.title,selected.text);}
        else if(view==View::Task)readerDraw(raster,(selectedTask.done?"Terminée · ":"")+selectedTask.title,selectedTask.details);
        else if(view==View::Event)readerDraw(raster,selectedEvent.title,civilLabel(selectedEvent.date)+" · "+(selectedEvent.all_day?"Journée entière":selectedEvent.time)+"\n\n"+selectedEvent.details);
        else if(view==View::Reader)readerDraw(raster,fileTitle,fileText);
        else if(view==View::Editor)editorDraw(raster);
        else if(view==View::Network&&height()!=800){
            ink.text(26,164,ssid+"   Clé : "+password,Face::Body);ink.text(26,205,"http://192.168.4.1 · accès direct sur Wi-Fi protégé",Face::Body);listDraw(raster,264);
        }else if(view==View::Network){
            ink.box(24,176,width()-48,172,16,false);ink.icon(Icon::Wifi,42,195,32);ink.text(90,194,wifiEnabled?"Votre réseau personnel":"Wi-Fi désactivé",Face::Small);ink.text(42,236,ssid,Face::Heading);ink.text(42,284,"Clé  "+password,Face::Body);
            ink.text(26,373,"Adresse du compagnon",Face::Small);ink.text(24,408,"http://192.168.4.1",Face::Heading);ink.text(26,456,"Accès direct sur votre Wi-Fi protégé.",Face::Small);ink.text(26,482,"L’invitation sur l’appareil est en pause.",Face::Small);listDraw(raster,536);
        }else if(view==View::Audio&&(audioState.state==AudioState::Recording||audioState.state==AudioState::Starting||audioState.state==AudioState::Saving||audioState.state==AudioState::Playing)){
            if(height()!=800){
                ink.icon(Icon::Mic,width()-88,170,46);const auto seconds=audioState.durationMs/1000;
                char duration[16];snprintf(duration,sizeof duration,"%02lu:%02lu",static_cast<unsigned long>(seconds/60),static_cast<unsigned long>(seconds%60));
                ink.text(26,174,audioState.state==AudioState::Recording?"Enregistrement":"Audio en cours",Face::Body);ink.text(24,214,duration,Face::Clock);
                ink.box(24,302,width()-48,58,14,true);ink.text(46,318,"Clic pour arrêter et conserver",Face::Body,true);
            }else{
            ink.box(24,184,width()-48,390,22,false);ink.icon(audioState.state==AudioState::Playing?Icon::Play:Icon::Mic,width()/2-36,224,72);
            const auto seconds=audioState.durationMs/1000;char elapsed[12];snprintf(elapsed,sizeof elapsed,"%02lu:%02lu",static_cast<unsigned long>(seconds/60),static_cast<unsigned long>(seconds%60));ink.text(112,340,elapsed,Face::Clock);
            const std::string state=audioState.state==AudioState::Recording?"Enregistrement en cours":audioState.state==AudioState::Playing?"Lecture de votre audio":audioState.state==AudioState::Saving?"Sauvegarde de l’original…":"Préparation audio…";
            ink.text(42,438,ink.fit(state,width()-84,Face::Body),Face::Body);ink.text(42,494,"4 minutes maximum · sauvegarde SD",Face::Small);
            ink.box(24,604,width()-48,66,14,true);ink.icon(Icon::Stop,44,622,28,true);ink.text(92,622,audioState.state==AudioState::Saving?"Merci de patienter":"Clic pour arrêter",Face::Body,true);
            }
        }else if(view==View::StorageInfo){uint64_t total=0,free=0;if(esp_vfs_fat_info("/sdcard",&total,&free)==ESP_OK){readerDraw(raster,std::to_string(free/1048576)+" Mo libres",std::to_string(total/1048576)+" Mo au total\n\n"+std::to_string(notes.size())+" notes\n"+std::to_string(tasks.size())+" tâches\n"+std::to_string(agenda.size())+" rendez-vous\n"+std::to_string(audios.size())+" enregistrements\n\nAucun formatage automatique.");}}
        else if(view==View::About)readerDraw(raster,"Mon compagnon 0.4","Aujourd’hui, vos notes, vos tâches, votre agenda local et vos audios.\n\nConçu pour l’e-paper et l’usage hors ligne.\n\nGoogle et Gemini viendront ensuite. La veille prolongée est accessible par appui long sur le bouton bas droit. La veille légère, PDF/EPUB et la météo restent à intégrer.\n\nPilotes issus de références attribuées, licence conservée.");
        else listDraw(raster);
    }
    if(view!=View::PowerDown&&view!=View::DeepScreen&&!message.empty()){auto lines=ink.wrap(message,width()-52,Face::Small);for(size_t i=0;i<lines.size()&&i<2;++i)ink.text(26,height()-78+i*21,lines[i],Face::Small);}
    if(view!=View::PowerDown&&view!=View::DeepScreen){ink.line(24,height()-36,width()-24,height()-36,true,1);ink.icon(Icon::Back,24,height()-26,18);ink.text(52,height()-28,powerKeyReady?"Bas droit : retour":"BOOT : retour",Face::Small);ink.text(width()-126,height()-28,editingNumber?"Ajuster / clic":"Clic : valider",Face::Small);}
    DisplayJob job{frame,!forceFull,0};if(xQueueSend(displayJobs,&job,0)==pdTRUE){std::swap(frame,spare);displayBusy=true;forceFull=false;++refreshCount;dirty=false;}
}
void storageTest() {
    auto result = server->localNotes([](NoteService& service) {
        auto created = service.create({"Test temporaire", "Verification stockage 0.2", std::nullopt});
        if (!created.ok()) return created;
        NoteStore reopened(*files);
        const auto recovered = reopened.recover();
        const auto* saved = reopened.find(created.note->id);
        if (recovered != NoteResult::Ok || !saved || saved->text != "Verification stockage 0.2") {
            NoteServiceResponse failed; failed.result = NoteResult::IoError; return failed;
        }
        return service.erase(created.note->id, created.note->version);
    });
    Serial.println(result.ok() ? "NOTES_TEST PASS" : "NOTES_TEST FAIL");
    bool taskOk = taskStore && taskStore->ready();
    Task test;
    if (taskOk) taskOk = taskStore->create("Test temporaire", "Test taches 0.2", &test) == NoteResult::Ok;
    if (taskOk) taskOk = taskStore->setDone(test.id, true, test.version, &test) == NoteResult::Ok;
    if (taskOk) {
        TaskStore reopened(*taskFiles); taskOk = reopened.recover() == NoteResult::Ok;
        const auto restored = reopened.tasks();
        taskOk = taskOk && std::any_of(restored.begin(), restored.end(), [&](const Task& task) { return task.id == test.id && task.done; });
    }
    if (taskOk) taskOk = taskStore->erase(test.id, test.version) == NoteResult::Ok;
    Serial.println(taskOk ? "TASKS_TEST PASS" : "TASKS_TEST FAIL");
    refreshNotes(); refreshTasks(); message = result.ok() && taskOk ? "Tests notes et taches reussis." : "Erreur stockage. Test temporaire possiblement conserve.";
}
}
void setup() {
    Serial.begin(115200); delay(200); Serial.println("MON COMPAGNON 0.4 AUJOURDHUI");
    frame = static_cast<uint8_t*>(ps_malloc(Raster::bytes)); spare = static_cast<uint8_t*>(ps_malloc(Raster::bytes));
    if (!frame || !spare) { Serial.println("STOP: screen allocation failed"); return; }
    deepWake=wokeFromDeepSleep();if(deepWake){for(int pin:{4,5,6})rtc_gpio_deinit(static_cast<gpio_num_t>(pin));}
    if (!beginBoard(fault)) { Serial.println(fault.c_str()); return; }
    displayJobs = xQueueCreate(1, sizeof(DisplayJob)); displayResults = xQueueCreate(1, sizeof(DisplayResult));
    if (!displayJobs || !displayResults || xTaskCreate(displayWorker, "compagnon-screen", 6144, nullptr, 1, nullptr) != pdPASS) { Serial.println("STOP: screen task failed"); return; }
    if (preferences.begin("compagnon", false)) {
        const auto saved = preferences.getUChar("orientation", 2);
        if (saved <= 2) orientation = static_cast<Orientation>(saved);
    }
    if(deepWake)preferences.putUInt("deep-wakes",preferences.getUInt("deep-wakes",0)+1);
    if (!mountNotes(fault)) { Serial.println(fault.c_str()); render(); return; }
    files.reset(new PosixNoteFiles(kNotesDirectory)); store.reset(new NoteStore(*files));
    if (store->recover() != NoteResult::Ok) { fault = "Recuperation des notes impossible. Fichiers conserves."; Serial.println(fault.c_str()); render(); return; }
    const char* tasksDirectory = "/sdcard/compagnon-tasks";
    if ((mkdir(tasksDirectory, 0700) == 0 || errno == EEXIST)) {
        taskFiles.reset(new PosixNoteFiles(tasksDirectory)); taskStore.reset(new TaskStore(*taskFiles));
        if (taskStore->recover() != NoteResult::Ok) taskError = "Fichiers taches illisibles. Aucun remplacement automatique.";
    } else taskError = "Repertoire des taches indisponible.";
    refreshTasks();
    const char* agendaDirectory="/sdcard/compagnon-agenda";
    if(mkdir(agendaDirectory,0700)==0||errno==EEXIST){agendaFiles.reset(new PosixNoteFiles(agendaDirectory));agendaStore.reset(new AgendaStore(*agendaFiles));if(agendaStore->recover()!=NoteResult::Ok)agendaError="Agenda local indisponible. Fichiers conservés.";}else agendaError="Dossier agenda indisponible.";
    if(mkdir("/sdcard/compagnon-audio-index",0700)==0||errno==EEXIST){recordingFiles.reset(new PosixNoteFiles("/sdcard/compagnon-audio-index"));recordingIndex.reset(new NoteStore(*recordingFiles));recordingIndex->recover();}
    volume=preferences.getUChar("volume",65);if(volume>100)volume=65;microphoneGain=preferences.getUChar("mic-gain",30);if(microphoneGain>42||microphoneGain%6)microphoneGain=30;
    showBatteryPercent=preferences.getBool("battery-pct",false);
    configureClockTimezone(preferences.getString("timezone","Europe/Madrid").c_str());
    beginClock(preferences.getBool("rtc-utc",false),clockError);clockNow=clockSnapshot();
    powerKeyReady=beginPowerKey(message,true);Serial.println(powerKeyReady?"POWER_HOLD_CONFIG PASS":"POWER_HOLD_CONFIG FAIL");battery=readBatterySnapshot(message);
    if(audio.begin(audioError)){audio.setVolume(volume,message);audio.setMicrophoneGain(microphoneGain,message);Serial.println("AUDIO_INIT PASS");}else{Serial.printf("AUDIO_INIT FAIL: %s\n",audioError.c_str());}
    audioState=audio.status();refreshAudios();refreshAgenda();
    httpResults=xQueueCreate(1,sizeof(HttpResult));
    if(mkdir("/sdcard/compagnon-postits",0700)==0||errno==EEXIST){postitFiles.reset(new PosixNoteFiles("/sdcard/compagnon-postits"));postitStore.reset(new PostitStore(*postitFiles));if(postitStore->recover()!=NoteResult::Ok)Serial.println("POSTIT_STYLE recovery unavailable");}
    if(postitStore)postitHttp.reset(new PostitHttp(*store,*postitStore));
    sdReady = true;
    notesApi.reset(new NoteHttp(*store));
    WiFi.persistent(false); WiFi.mode(WIFI_AP);
    ssid=preferences.getString("ap-ssid","").c_str();password=preferences.getString("ap-key","").c_str();
    if(!preferences.getBool("ap-ready",false)||ssid.empty()||password.size()<12){
        unsigned char random[16];esp_fill_random(random,sizeof random);
        char name[32];snprintf(name,sizeof name,"COMPAGNON-%02X%02X",random[0],random[1]);ssid=name;
        constexpr char chars[]="ABCDEFGHJKMNPQRSTUVWXYZ23456789";password.clear();
        for(int i=2;i<14;++i)password+=chars[random[i]%(sizeof chars-1)];
        preferences.putBool("ap-ready",false);preferences.putString("ap-ssid",ssid.c_str());preferences.putString("ap-key",password.c_str());
        if(std::string(preferences.getString("ap-key","").c_str())==password&&std::string(preferences.getString("ap-ssid","").c_str())==ssid)preferences.putBool("ap-ready",true);
    }
    apCredentialsStable=preferences.getBool("ap-ready",false);
    wifiEnabled = apCredentialsStable && WiFi.softAP(ssid.c_str(), password.c_str(), 1, false, 1);
    server.reset(new EmbeddedHttp("http://192.168.4.1", notesApi.get(), PairingMode::LocalPassword, true, postitHttp.get()));
    if (wifiEnabled) webReady = server->begin() == ESP_OK;
    refreshNotes();
    message = wifiEnabled && webReady ? "Bienvenue. Ouvrir Applications pour commencer." : apCredentialsStable?"Mode local actif. Compagnon web indisponible.":"Mémoire Wi-Fi indisponible. Identifiants non modifiés.";
    Serial.println("READY 0.4: Today + stable AP + audio + agenda + postits + deep sleep");
    if(deepWake){chooseSleepPostit();if(selected.pinned)go(View::Note);for(auto&button:buttons){button.raw=button.stable=digitalRead(button.pin)==LOW;button.changed=millis();}}
    render();
}
void loop() {
    if (!frame || !displayResults) { delay(20); return; }
    DisplayResult result;
    if (xQueueReceive(displayResults, &result, 0) == pdTRUE) {
        displayBusy = false;
        if (!result.ok) {
            fault = result.error; Serial.println(result.error);
            // No usable physical consent/revocation UI: close private access.
            if (server) server->revoke();
            WiFi.softAPdisconnect(true); wifiEnabled = false; webActive = false;
        }
        else if (result.request && result.request == showingRequest) approvalDisplayed = true;
    }
    if (!server || !fault.empty()) { delay(20); return; }
    const auto now = millis();
    if(Serial.available()&&!pendingPowerOff&&!pendingDeepSleep){
        const char command=Serial.read();
        if(command=='t'){storageTest();dirty=true;}
        else if(command=='w'&&!httpTesting&&httpResults){httpTesting=true;if(xTaskCreate(httpWorker,"http-check",6144,nullptr,1,nullptr)!=pdPASS)httpTesting=false;}
        else if(command=='a'){toggleRecording();}
        else if(command=='d'){openPowerMenu();}
        else if(command=='r'){auto state=audio.status();std::string result;if(state.state==AudioState::Idle||state.state==AudioState::Fault){refreshAudios();const auto path=state.lastSavedPath.empty()&&!audios.empty()?audios.front().path:state.lastSavedPath;audioDiagnostics(path,result);Serial.println(result.c_str());}}
        else if(command=='p'){refreshAudios();auto state=audio.status();const auto path=state.lastSavedPath.empty()&&!audios.empty()?audios.front().path:state.lastSavedPath;if(!path.empty())audio.play(path,message);}
        else if(command=='s'){audio.stopPlayback();audio.stopRecording();}
        else if(command=='c'){Serial.setTimeout(100);std::string value=Serial.readStringUntil('\n').c_str();if(value.size()==16&&value[10]==' '){bool ok=applyClock(value.substr(0,10),value.substr(11));Serial.println(ok?"CLOCK_SET PASS":"CLOCK_SET FAIL");dirty=true;}}
        else if(command=='i'){auto state=audio.status();Serial.printf("STATUS audio=%u ms=%lu saved=%u clock=%u APstable=%u\n",static_cast<unsigned>(state.state),static_cast<unsigned long>(state.durationMs),!state.lastSavedPath.empty(),clockNow.valid,preferences.getBool("ap-ready",false));Serial.printf("HARDWARE power=%u battery=%d deep_wakes=%u\n",powerKeyReady,battery.percent,preferences.getUInt("deep-wakes",0));}
    }
    if(httpResults){HttpResult check;if(xQueueReceive(httpResults,&check,0)==pdTRUE){httpTesting=false;Serial.println(check.summary);Serial.println(check.ok?"HTTP_SELF_TEST PASS":"HTTP_SELF_TEST FAIL");message=check.ok?"Page et ressources web transmises intégralement.":"Diagnostic web en échec. Voir USB.";dirty=true;}}
    const auto currentAudio=audio.status();
    if(currentAudio.state!=audioState.state){queuedConfirm=0;++uiRevision;audioState=currentAudio;dirty=true;if(audioState.state==AudioState::Fault)message=audioState.error;}
    else audioState=currentAudio;
    if(audioState.lastSavedPath!=lastAudioSaved&&!audioState.lastSavedPath.empty()){
        lastAudioSaved=audioState.lastSavedPath;selectedAudio={lastAudioSaved,audioState.durationMs};audioCategory=5;saveAudioCategory();refreshAudios();go(View::AudioType);message=audioState.error.empty()?"Original sauvegardé. Choisir son type.":"Capture interrompue : "+audioState.error+" Audio partiel conservé.";
    }
    if((audioState.state==AudioState::Recording||audioState.state==AudioState::Playing)&&now-lastAudioPaint>=2000){lastAudioPaint=now;dirty=true;}
    if(now-lastSensorPoll>=1000){if(now/30000!=lastSensorPoll/30000){std::string error;battery=readBatterySnapshot(error);}lastSensorPoll=now;auto updated=clockSnapshot();if(updated.time!=clockNow.time||updated.valid!=clockNow.valid){clockNow=updated;dirty=true;}}

    const bool active = server->approved(); if (active != webActive) { webActive = active; dirty = true; }
    const auto request = server->pendingRequestId();
    if (request != showingRequest) { queuedConfirm = 0; showingRequest = request; showingApproval = request != 0; approvalDisplayed = false; approvalArmed = false; dirty = true; }
    if (showingApproval && approvalDisplayed && !approvalArmed && digitalRead(4) == HIGH && digitalRead(5) == HIGH && digitalRead(6) == HIGH && digitalRead(0) == HIGH) approvalArmed = true;
    for(int i=0;i<3;++i)if(buttons[i].pressed(now,!showingApproval&&(i==0||i==2)))input(i);
    if(buttons[3].pressed(now,false)){input(3);backLongHandled=false;}
    if(!pendingPowerOff&&!pendingDeepSleep&&buttons[3].stable&&!backLongHandled&&now-buttons[3].changed>=1500){backLongHandled=true;openPowerMenu();}
    if(!buttons[3].stable)backLongHandled=false;
    if(powerKeyReady){const auto power=pollPowerKey();
        if(power==PowerKeyEvent::Pressed&&!pendingPowerOff&&!pendingDeepSleep){
            if(audioState.state==AudioState::Idle||audioState.state==AudioState::Fault){if(canSuspendEditor()&&audio.startRecording(message)){suspendEditor();holdCapture=true;go(View::Audio);}}
            else if(audioState.state==AudioState::Recording)holdCapture=true;
        }else if(power==PowerKeyEvent::Released&&holdCapture){audio.stopRecording();holdCapture=false;dirty=true;}
    }
    if(now-lastPoll>=5000){lastPoll=now;refreshNotes();if(view==View::Note&&selected.pinned){auto version=selectedStyle.version;loadPostitStyle(selected.id);if(version!=selectedStyle.version)dirty=true;}}
    if(pendingDeepSleep&&!dirty&&!displayBusy){
        const auto live=audio.status();
        if(live.state==AudioState::Recording||live.state==AudioState::Starting||live.state==AudioState::Saving||live.state==AudioState::Playing){pendingDeepSleep=false;go(View::Audio);message="Terminer l’audio avant la veille.";return;}
        if(!wakeKeysReleased()){wakeReleasedSince=0;return;}
        if(!wakeReleasedSince){wakeReleasedSince=now;return;}
        if(now-wakeReleasedSince<150)return;
        std::string error;if(!armDeepSleep(error)){pendingDeepSleep=false;go(View::Home);message=error;return;}
        if(server){server->revoke();server.reset();}WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);digitalWrite(39,LOW);
        Serial.println("DEEP_SLEEP ENTER external rocker wake only");Serial.flush();enterDeepSleep();
    }
    if(pendingPowerOff&&!dirty&&!displayBusy){
        const auto live=audio.status();
        if(live.state==AudioState::Recording||live.state==AudioState::Starting||live.state==AudioState::Saving||live.state==AudioState::Playing){pendingPowerOff=false;go(View::Audio);message="Extinction annulée. Terminer et sauvegarder l’audio.";return;}
        pendingPowerOff=false;if(server){server->revoke();server.reset();}WiFi.softAPdisconnect(true);wifiEnabled=false;
        if(!requestPowerOff(message)){fault=message;Serial.println("POWER_OFF_REQUEST FAIL");}else Serial.println("POWER_OFF_REQUEST ACCEPTED");
        return;
    }
    if (queuedConfirm && !dirty && !displayBusy && !showingApproval) {
        const bool matching = queuedConfirm == uiRevision;
        queuedConfirm = 0;
        if (matching) input(1);
    }
    if (dirty && !displayBusy && now - lastInput >= 70) render();
    delay(5);
}
