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
using namespace compagnon;
using namespace compagnon::device;
namespace {
std::unique_ptr<PosixNoteFiles> files, taskFiles;
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
    Editor, Discard, DeleteNote, DeleteTask, Files, Reader, Network, Settings, Help };
enum class Edit { NewNote, NewPin, NoteText, NoteTitle, NewTask, TaskDetails, TaskTitle };
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
void go(View next) { view = next; cursor = 0; page = 0; dirty = true; }
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
void startEdit(Edit kind) {
    edit = kind; editorReturn = view; titleField = true; alphabetMode = 0; key = 0;
    draftTitle.clear(); draftText.clear();
    if (kind == Edit::NoteText || kind == Edit::NoteTitle) { draftTitle = selected.title; draftText = selected.text; titleField = kind == Edit::NoteTitle; }
    if (kind == Edit::TaskDetails || kind == Edit::TaskTitle) { draftTitle = selectedTask.title; draftText = selectedTask.details; titleField = kind == Edit::TaskTitle; }
    go(View::Editor); message = "Choisir les caracteres, puis OK.";
}
bool editingTask() { return edit == Edit::NewTask || edit == Edit::TaskDetails || edit == Edit::TaskTitle; }
void finishEdit() {
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
        refreshNotes(); go(View::Note); message = "Note sauvegardee sur SD.";
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
    case View::Home: return {"Applications", "Nouvelle note", "Nouvelle tache", "Mes post-it", "Compagnon web"};
    case View::Apps: return {"Notes", "Post-it", "Taches", "Fichiers texte", "Compagnon web", "Reglages", "Aide"};
    case View::Notes: case View::Pins: {
        std::vector<std::string> out{view == View::Pins ? "+ NOUVEAU POST-IT" : "+ NOUVELLE NOTE"};
        for (const auto& note : visibleNotes()) out.push_back((note.pinned ? "* " : "") + (note.title.empty() ? "Sans titre" : note.title));
        return out;
    }
    case View::NoteActions: return {selected.pinned ? "Desepingler" : "Epingler et afficher", "Modifier le texte", "Renommer", "Supprimer", "Retour a la note"};
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
    case View::Settings: return {"Orientation : " + std::string(orientation == Orientation::Portrait ? "PORTRAIT" : orientation == Orientation::PortraitInverted ? "PORTRAIT INVERSE" : "PAYSAGE"), "Nettoyer l'ecran", "Informations stockage", "Aide et commandes", "Retour aux applications"};
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
    if (view == View::Home) return;
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
        WiFi.mode(WIFI_AP); wifiEnabled = WiFi.softAP(ssid.c_str(), password.c_str(), 1, false, 1);
        if (wifiEnabled && !webReady) webReady = server->begin() == ESP_OK;
        message = wifiEnabled && webReady ? "Compagnon web disponible." : "Reseau indisponible. Fonctions locales actives.";
    }
    dirty = true;
}
void choose() {
    switch (view) {
    case View::Home:
        if (cursor == 0) go(View::Apps); else if (cursor == 1) startEdit(Edit::NewNote);
        else if (cursor == 2) { if (taskStore && taskStore->ready()) startEdit(Edit::NewTask); else { go(View::Tasks); message = taskError; } }
        else if (cursor == 3) go(View::Pins); else go(View::Network); break;
    case View::Apps: {
        const View pages[] = {View::Notes, View::Pins, View::Tasks, View::Files, View::Network, View::Settings, View::Help};
        const auto next = pages[cursor]; if (next == View::Files) { folder.clear(); loadFolder(); } else go(next); break;
    }
    case View::Notes: case View::Pins: {
        const bool pins = view == View::Pins;
        const auto available = visibleNotes();
        if (cursor == 0) startEdit(pins ? Edit::NewPin : Edit::NewNote);
        else if (static_cast<std::size_t>(cursor - 1) < available.size()) { selected = available[cursor - 1]; go(View::Note); } break;
    }
    case View::Note: go(View::NoteActions); break;
    case View::NoteActions:
        if (cursor == 0) {
            auto result = server->localNotes([&](NoteService& service) { return service.setPinned(selected.id, !selected.pinned, selected.version); });
            if (result.ok()) { selected = *result.note; refreshNotes(); go(View::Note); message = selected.pinned ? "Post-it affiche." : "Post-it desepingle."; }
            else message = result.message;
        } else if (cursor == 1) startEdit(Edit::NoteText); else if (cursor == 2) startEdit(Edit::NoteTitle);
        else if (cursor == 3) go(View::DeleteNote); else go(View::Note); break;
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
            if (result.ok()) { refreshNotes(); go(View::Notes); message = "Note supprimee."; } else message = result.message;
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
    case View::Settings:
        if (cursor == 0) {
            orientation = orientation == Orientation::Portrait ? Orientation::PortraitInverted : orientation == Orientation::PortraitInverted ? Orientation::Landscape : Orientation::Portrait;
            if (preferences.putUChar("orientation", static_cast<uint8_t>(orientation)) != 1) message = "Orientation active, memorisation echouee.";
            else message = "Orientation memorisee.";
            forceFull = true;
        } else if (cursor == 1) { forceFull = true; message = "Rafraichissement complet demande."; }
        else if (cursor == 2) {
            uint64_t total = 0, free = 0;
            if (esp_vfs_fat_info("/sdcard", &total, &free) == ESP_OK) message = "SD : " + std::to_string(free / 1048576) + " Mo libres / " + std::to_string(total / 1048576) + " Mo.";
            else message = "Informations SD indisponibles.";
        } else if (cursor == 3) go(View::Help); else go(View::Apps); break;
    case View::Help: go(View::Apps); break;
    default: break;
    }
}
void input(int event) {
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
    if (event == 3) { back(); return; }
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
    if (view == View::Note || view == View::Task || view == View::Reader || view == View::Help) {
        if (event == 0 && page) --page;
        else if (event == 2) ++page; // Clamped to actual pages at render.
        return;
    }
    const auto items = labels();
    if (!items.empty()) cursor = (cursor + items.size() + (event == 0 ? -1 : 1)) % items.size();
}
std::string viewTitle() {
    switch (view) {
    case View::Home: return "Accueil"; case View::Apps: return "Applications";
    case View::Notes: return "Mes notes"; case View::Pins: return "Mes post-it";
    case View::Note: return selected.pinned ? "Post-it" : "Note"; case View::NoteActions: return "Actions note";
    case View::Tasks: return "Mes taches"; case View::Task: return "Tache"; case View::TaskActions: return "Actions tache";
    case View::Files: return "Fichiers texte"; case View::Reader: return "Lecture";
    case View::Network: return "Compagnon web"; case View::Settings: return "Reglages";
    case View::Help: return "Aide"; case View::Editor: return titleField ? "Saisir le titre" : "Saisir le texte";
    case View::Discard: return "Brouillon"; default: return "Supprimer ?";
    }
}
void listDraw(Raster& raster, int startY = -1) {
    const auto items = labels(); if (items.empty()) return;
    cursor = std::max(0, std::min(cursor, static_cast<int>(items.size()) - 1));
    const int top = startY < 0 ? contentY() : startY;
    const int visible = std::max(1, (height() - 112 - top) / 54);
    const int start = (cursor / visible) * visible;
    for (int i = start; i < static_cast<int>(items.size()) && i < start + visible; ++i) {
        const int y = top + (i - start) * 54;
        if (cursor == i) raster.bar(14, y - 8, width() - 28, 42);
        raster.text(24, y + 2, shortLabel(items[i], columns() - 3), 2, cursor == i);
    }
    raster.text(width() - 104, height() - 114, std::to_string(cursor + 1) + "/" + std::to_string(items.size()), 2);
}
void readerDraw(Raster& raster, const std::string& title, const std::string& body) {
    // Titles and contents share the paginated document, so neither is truncated.
    const auto lines = wrapText(title + "\n\n" + (body.empty() ? "(Sans texte)" : body), columns());
    const auto perPage = static_cast<std::size_t>(std::max(1, (height() - 150 - contentY()) / 26));
    const auto count = std::max<std::size_t>(1, (lines.size() + perPage - 1) / perPage);
    page = std::min(page, count - 1);
    for (std::size_t i = page * perPage; i < lines.size() && i < (page + 1) * perPage; ++i)
        raster.text(24, contentY() + (i - page * perPage) * 26, lines[i], 2);
    raster.text(24, height() - 132, "PAGE " + std::to_string(page + 1) + "/" + std::to_string(count), 2);
}
void editorDraw(Raster& raster) {
    const auto& value = titleField ? draftTitle : draftText;
    raster.text(24, contentY(), std::to_string(value.size()) + (titleField ? "/160 OCTETS | " : "/4096 OCTETS | ") + (alphabetMode == 0 ? "MIN" : alphabetMode == 1 ? "MAJ" : "123"), 2);
    const auto chars = characters();
    std::string caption = "Selection puis clic pour inserer.";
    if (key >= 6 && static_cast<std::size_t>(key - 6) < chars.size()) {
        const auto& c = chars[key - 6];
        if (c == "é") caption = "E ACCENT AIGU"; else if (c == "è") caption = "E ACCENT GRAVE";
        else if (c == "ê") caption = "E ACCENT CIRCONFLEXE"; else if (c == "à") caption = "A ACCENT GRAVE";
        else if (c == "ç") caption = "C CEDILLE"; else if (c == "ù") caption = "U ACCENT GRAVE";
        else if (c == "ô") caption = "O ACCENT CIRCONFLEXE"; else if (c == "î") caption = "I ACCENT CIRCONFLEXE";
    }
    raster.text(24, contentY() + 24, caption, 1);
    const int cols = std::min(13, (width() - 40) / 42);
    const int rows = (chars.size() + cols - 1) / cols;
    const int keysY = height() - 112 - rows * 32;
    const int commandsY = keysY - 76;
    const int previewY = contentY() + 44;
    const int previewRows = std::max(1, (commandsY - 16 - previewY) / 24);
    const auto textLines = wrapText(value + "_", columns());
    const auto start = textLines.size() > static_cast<std::size_t>(previewRows) ? textLines.size() - previewRows : 0;
    for (std::size_t i = start; i < textLines.size(); ++i) raster.text(24, previewY + (i - start) * 24, textLines[i], 2);
    const std::string commands[] = {titleField && (edit == Edit::NewNote || edit == Edit::NewPin || edit == Edit::NewTask) ? "SUIVANT" : "SAUVER", "ESPACE", "EFFACER", "LIGNE", "ANNULER", alphabetMode == 0 ? "ABC" : alphabetMode == 1 ? "123" : "a-z"};
    const int cell = (width() - 32) / 3;
    for (int i = 0; i < 6; ++i) {
        const int x = 20 + (i % 3) * cell, y = commandsY + (i / 3) * 36;
        if (key == i) raster.bar(x - 4, y - 6, cell - 8, 30);
        raster.text(x + 4, y, commands[i], 2, key == i);
    }
    for (std::size_t i = 0; i < chars.size(); ++i) {
        const int x = 24 + (i % cols) * 42, y = keysY + (i / cols) * 32;
        if (key == static_cast<int>(i) + 6) raster.bar(x - 5, y - 5, 32, 28);
        raster.text(x, y, chars[i], 2, key == static_cast<int>(i) + 6);
    }
}
void render() {
    if (!frame || displayBusy) return;
    Raster raster(frame, orientation); raster.clear();
    const bool portrait = height() == 800;
    raster.text(20, portrait ? 16 : 8, "MON COMPAGNON", portrait ? 3 : 2);
    raster.text(24, portrait ? 52 : 34, std::string(sdReady ? "SD OK | " : "SD ERREUR | ") + std::string(wifiEnabled ? "WI-FI LOCAL" : "WI-FI COUPE") + (webActive ? " | WEB ACTIF" : ""), portrait ? 2 : 1);
    raster.bar(14, portrait ? 80 : 54, width() - 28, 2);
    raster.text(22, portrait ? 100 : 68, showingApproval ? "Acces demande" : viewTitle(), portrait ? 3 : 2);
    raster.text(24, portrait ? 136 : 98, "HAUT/BAS : CHOISIR   CLIC : OUVRIR", 1);
    if (showingApproval) {
        const auto lines = wrapText("Autoriser ce navigateur ?\n\nN'acceptez que votre demande.\n\nRelachez les commandes, puis :\nCLIC : ACCEPTER\nBOOT : REFUSER", columns());
        for (std::size_t i = 0; i < lines.size(); ++i) raster.text(24, contentY() + i * 28, lines[i], 2);
    } else if (!fault.empty()) readerDraw(raster, "DEMARRAGE INTERROMPU", fault + "\nAucun formatage automatique.");
    else if (view == View::Home) {
        const auto pending = std::count_if(tasks.begin(), tasks.end(), [](const Task& task) { return !task.done; });
        raster.text(24, contentY(), std::to_string(notes.size()) + " NOTES | " + (taskStore && taskStore->ready() ? std::to_string(pending) + " TACHES A FAIRE" : "TACHES INDISP."), 2);
        listDraw(raster, contentY() + 52);
    } else if (view == View::Note) readerDraw(raster, selected.title, selected.text);
    else if (view == View::Task) readerDraw(raster, std::string(selectedTask.done ? "[X] " : "[ ] ") + selectedTask.title, selectedTask.details);
    else if (view == View::Reader) readerDraw(raster, fileTitle, fileText);
    else if (view == View::Editor) editorDraw(raster);
    else if (view == View::Network) {
        const auto lines = wrapText(wifiEnabled && webReady ? ssid + "\nCLE : " + password + "\n\nhttp://192.168.4.1\nPuis demander l'acces web." : "Compagnon web indisponible.\nCouper/reactiver le Wi-Fi pour reessayer.\nLes fonctions locales restent actives.", columns());
        for (std::size_t i = 0; i < lines.size(); ++i) raster.text(24, contentY() + i * 26, lines[i], 2);
        listDraw(raster, contentY() + (height() == 800 ? 240 : 150));
    } else if (view == View::Help) {
        readerDraw(raster, "MON COMPAGNON 0.2", "Haut/bas : choisir. Maintenir pour defiler.\nClic : ouvrir ou valider.\nBOOT : retour.\n\nNotes : titre, texte et actions.\nPost-it : epingler et afficher.\nTaches : creer et cocher.\nFichiers : TXT/MD sur la SD.\n\nDans le clavier : SUIVANT, puis SAUVER. ABC/123 change les caracteres.\n\nAudio, Google et veilles restent a integrer. PWR peut couper l'alimentation.\nLes accents sont conserves sur SD, simplifies a l'ecran.");
    } else if (view == View::Tasks && (!taskStore || !taskStore->ready())) readerDraw(raster, "Stockage taches indisponible", taskError);
    else listDraw(raster);
    const auto notices = wrapText(message, columns());
    for (std::size_t i = 0; i < notices.size() && i < 2; ++i) raster.text(24, height() - 90 + i * 20, notices[i], 2);
    const std::string hint = view == View::Note || view == View::Task ? "CLIC : ACTIONS | BOOT : RETOUR" : "CLIC : VALIDER | BOOT : RETOUR";
    raster.text(24, height() - 34, hint, 1);
    DisplayJob job{frame, !forceFull && refreshCount % 10 != 0, showingApproval ? showingRequest : 0};
    if (xQueueSend(displayJobs, &job, 0) == pdTRUE) {
        std::swap(frame, spare); displayBusy = true; forceFull = false; ++refreshCount; dirty = false;
    }
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
    Serial.begin(115200); delay(200); Serial.println("MON COMPAGNON 0.2 PORTRAIT");
    frame = static_cast<uint8_t*>(ps_malloc(Raster::bytes)); spare = static_cast<uint8_t*>(ps_malloc(Raster::bytes));
    if (!frame || !spare) { Serial.println("STOP: screen allocation failed"); return; }
    if (!beginBoard(fault)) { Serial.println(fault.c_str()); return; }
    displayJobs = xQueueCreate(1, sizeof(DisplayJob)); displayResults = xQueueCreate(1, sizeof(DisplayResult));
    if (!displayJobs || !displayResults || xTaskCreate(displayWorker, "compagnon-screen", 6144, nullptr, 1, nullptr) != pdPASS) { Serial.println("STOP: screen task failed"); return; }
    if (preferences.begin("compagnon", false)) {
        const auto saved = preferences.getUChar("orientation", 2);
        if (saved <= 2) orientation = static_cast<Orientation>(saved);
    }
    if (!mountNotes(fault)) { Serial.println(fault.c_str()); render(); return; }
    files.reset(new PosixNoteFiles(kNotesDirectory)); store.reset(new NoteStore(*files));
    if (store->recover() != NoteResult::Ok) { fault = "Recuperation des notes impossible. Fichiers conserves."; Serial.println(fault.c_str()); render(); return; }
    const char* tasksDirectory = "/sdcard/compagnon-tasks";
    if ((mkdir(tasksDirectory, 0700) == 0 || errno == EEXIST)) {
        taskFiles.reset(new PosixNoteFiles(tasksDirectory)); taskStore.reset(new TaskStore(*taskFiles));
        if (taskStore->recover() != NoteResult::Ok) taskError = "Fichiers taches illisibles. Aucun remplacement automatique.";
    } else taskError = "Repertoire des taches indisponible.";
    refreshTasks();
    sdReady = true;
    notesApi.reset(new NoteHttp(*store));
    WiFi.persistent(false); WiFi.mode(WIFI_AP);
    unsigned char random[16]; esp_fill_random(random, sizeof random);
    char name[32]; snprintf(name, sizeof name, "COMPAGNON-%02X%02X", random[0], random[1]); ssid = name;
    constexpr char chars[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    for (int i = 2; i < 14; ++i) password += chars[random[i] % (sizeof chars - 1)];
    wifiEnabled = WiFi.softAP(ssid.c_str(), password.c_str(), 1, false, 1);
    server.reset(new EmbeddedHttp("http://192.168.4.1", notesApi.get()));
    if (wifiEnabled) webReady = server->begin() == ESP_OK;
    refreshNotes();
    message = wifiEnabled && webReady ? "Bienvenue. Ouvrir Applications pour commencer." : "Mode local actif. Compagnon web indisponible.";
    Serial.println("READY 0.2: portrait + menus + notes + tasks + text files");
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
    if (Serial.available() && Serial.read() == 't') { storageTest(); dirty = true; }
    const bool active = server->approved(); if (active != webActive) { webActive = active; dirty = true; }
    const auto request = server->pendingRequestId();
    if (request != showingRequest) { queuedConfirm = 0; showingRequest = request; showingApproval = request != 0; approvalDisplayed = false; approvalArmed = false; dirty = true; }
    if (showingApproval && approvalDisplayed && !approvalArmed && digitalRead(4) == HIGH && digitalRead(5) == HIGH && digitalRead(6) == HIGH && digitalRead(0) == HIGH) approvalArmed = true;
    for (int i = 0; i < 4; ++i) if (buttons[i].pressed(now, !showingApproval && (i == 0 || i == 2))) input(i);
    if (now - lastPoll >= 5000) { lastPoll = now; refreshNotes(); }
    if (queuedConfirm && !dirty && !displayBusy && !showingApproval) {
        const bool matching = queuedConfirm == uiRevision;
        queuedConfirm = 0;
        if (matching) input(1);
    }
    if (dirty && !displayBusy && now - lastInput >= 70) render();
    delay(5);
}
