// Mon compagnon alpha: local notes + physically approved web, no cloud/audio/sleep.
#include <Arduino.h>
#include <WiFi.h>
#include <memory>
#include <algorithm>
#include "board.hpp"
#include "raster.hpp"
#include "posix_note_files.hpp"
#include "embedded_http.hpp"

using namespace compagnon;
using namespace compagnon::device;
namespace {
std::unique_ptr<PosixNoteFiles> files;
std::unique_ptr<NoteStore> store;
std::unique_ptr<NoteHttp> notesApi;
std::unique_ptr<EmbeddedHttp> server;
uint8_t* frame = nullptr;
std::vector<Note> notes;
std::string fault, message, ssid, password, draft;
Note selected;
enum class View { List, Note, Actions, Editor, Delete, Network };
View view = View::List;
int cursor = 0, action = 0, key = 0;
std::size_t page = 0;
bool editingExisting = false, dirty = true, showingApproval = false;
bool approvalDisplayed = false, approvalArmed = false;
std::uint64_t showingRequest = 0;
bool webActive = false;
unsigned long lastPoll = 0;
const std::string alphabet = "abcdefghijklmnopqrstuvwxyz0123456789.,!?-";
constexpr int commands = 5; // save, space, backspace, newline, cancel

struct Button {
    uint8_t pin; bool raw = false, stable = false; unsigned long changed = 0;
    bool pressed(unsigned long now) {
        const bool value = digitalRead(pin) == LOW;
        if (value != raw) { raw = value; changed = now; }
        if (stable != raw && now - changed >= 30) { stable = raw; return stable; }
        return false;
    }
};
Button buttons[] = {{4}, {5}, {6}, {0}};

NoteServiceResponse localList() { return server->localNotes([](NoteService& service) { return service.list(); }); }
void refreshNotes() {
    if (!server) return;
    auto result = localList();
    if (!result.ok()) { message = result.message; dirty = true; return; }
    bool changed = notes.size() != result.notes.size();
    for (std::size_t i = 0; !changed && i < notes.size(); ++i)
        changed = notes[i].id != result.notes[i].id || notes[i].version != result.notes[i].version;
    if (changed) {
        notes = std::move(result.notes); dirty = true;
        if (view == View::Note || view == View::Actions || view == View::Delete) {
            const auto found = std::find_if(notes.begin(), notes.end(), [](const Note& note) { return note.id == selected.id; });
            if (found == notes.end()) { view = View::List; message = "Note supprimee depuis le web."; }
            else if (view == View::Note) { selected = *found; page = std::min(page, (wrapText(selected.text, 60).size() - 1) / 8); }
            // Keep the old revision in action/delete/editor views so a remote
            // change produces a conflict instead of silently authorizing it.
        }
    }
    if (cursor >= static_cast<int>(notes.size()) + 3) cursor = 0;
}
void outcome(const NoteServiceResponse& result, const std::string& success) {
    message = result.ok() ? success : result.message;
    if (result.ok()) { view = View::List; refreshNotes(); }
    dirty = true;
}
void saveDraft() {
    if (draft.empty()) { message = "Saisissez du texte avant de sauvegarder."; return; }
    NoteServiceResponse result;
    if (editingExisting) result = server->localNotes([&](NoteService& service) {
        return service.update(selected.id, {selected.title, draft, selected.pinned, selected.version, std::nullopt});
    });
    else result = server->localNotes([&](NoteService& service) {
        return service.create({"Note locale", draft, std::nullopt});
    });
    outcome(result, "Note sauvegardee sur SD.");
}
void input(int event) { // up=0 select=1 down=2 BOOT/back=3
    dirty = true;
    if (showingApproval) {
        if (!approvalDisplayed || !approvalArmed) return;
        if (event == 1 || event == 3) {
            approvalArmed = false;
            const auto result = server->physicalDecision(event == 1, showingRequest);
            message = result == Result::Ok ? (event == 1 ? "Acces web autorise." : "Acces web refuse.") : "Demande expiree ou occupee.";
        }
        return;
    }
    if (!fault.empty()) return;
    if (event == 3) {
        if (view == View::Editor && !draft.empty()) { message = "Choisir ANNULER pour abandonner le brouillon."; return; }
        view = View::List; return;
    }
    const int direction = event == 0 ? -1 : 1;
    if (view == View::List) {
        const int count = static_cast<int>(notes.size()) + 3;
        if (event != 1) cursor = (cursor + count + direction) % count;
        else if (cursor == 0) { view = View::Editor; editingExisting = false; draft.clear(); key = 0; }
        else if (cursor <= static_cast<int>(notes.size())) { selected = notes[cursor - 1]; page = 0; view = View::Note; }
        else if (cursor == static_cast<int>(notes.size()) + 1) view = View::Network;
        else { server->revoke(); message = "Acces web revoque."; }
    } else if (view == View::Note) {
        const auto lines = wrapText(selected.text, 60);
        const auto pages = (lines.size() + 7) / 8;
        if (event == 1) { view = View::Actions; action = 0; }
        else if (event == 0 && page > 0) --page;
        else if (event == 2 && page + 1 < pages) ++page;
    } else if (view == View::Actions) {
        if (event != 1) action = (action + 4 + direction) % 4;
        else if (action == 0) {
            auto result = server->localNotes([&](NoteService& service) {
                return service.setPinned(selected.id, !selected.pinned, selected.version);
            });
            outcome(result, selected.pinned ? "Post-it desepingle." : "Post-it epingle.");
            if (result.note) { selected = *result.note; page = 0; view = View::Note; }
        }
        else if (action == 1) { editingExisting = true; draft = selected.text; key = 0; view = View::Editor; }
        else if (action == 2) { view = View::Delete; action = 0; }
        else view = View::List;
    } else if (view == View::Delete) {
        if (event != 1) action = 1 - action;
        else if (action == 0) view = View::Note;
        else outcome(server->localNotes([&](NoteService& service) {
            return service.erase(selected.id, selected.version);
        }), "Note supprimee.");
    } else if (view == View::Editor) {
        const int count = commands + static_cast<int>(alphabet.size());
        if (event != 1) key = (key + count + direction) % count;
        else if (key == 0) saveDraft();
        else if (key == 4) { draft.clear(); view = View::List; message = "Brouillon abandonne."; }
        else if (key == 2) {
            // Remove a complete UTF-8 codepoint, preserving original web accents.
            if (!draft.empty()) { std::size_t end = draft.size() - 1; while (end && (static_cast<unsigned char>(draft[end]) & 0xc0) == 0x80) --end; draft.resize(end); }
        } else if (draft.size() < NoteStore::max_text_bytes) draft += key == 1 ? ' ' : key == 3 ? '\n' : alphabet[key - commands];
        else message = "Texte plein : sauvegarder ou effacer.";
    }
}
void line(Raster& raster, int row, const std::string& text, bool highlight = false) {
    const int y = 62 + row * 34;
    if (highlight) raster.bar(12, y - 4, 776, 28);
    raster.text(20, y, text, 2, highlight);
}
void render() {
    Raster raster(frame); raster.clear();
    raster.text(18, 12, "MON COMPAGNON - ALPHA NOTES", 3);
    raster.text(638, 20, webActive ? "WEB ACTIF" : "WEB FERME", 2);
    raster.bar(12, 48, 776, 2);
    if (showingApproval) {
        line(raster, 1, "AUTORISER CE NAVIGATEUR ?");
        line(raster, 3, "CLIC CENTRAL : ACCEPTER");
        line(raster, 4, "BOOT : REFUSER");
        line(raster, 6, "N'acceptez que votre propre demande.");
    } else if (!fault.empty()) {
        line(raster, 0, "DEMARRAGE INTERROMPU");
        const auto lines = wrapText(fault, 60);
        for (std::size_t i = 0; i < lines.size() && i < 6; ++i) line(raster, i + 1, lines[i]);
        line(raster, 8, "Aucun formatage. Voir USB pour diagnostic.");
    } else if (view == View::List) {
        const int start = (cursor / 7) * 7;
        std::vector<std::string> labels{"+ NOUVELLE NOTE"};
        for (const auto& note : notes) labels.push_back((note.pinned ? "* " : "  ") + note.title);
        labels.push_back("CONNEXION WEB / WI-FI"); labels.push_back("REVOQUER ACCES WEB");
        for (int i = start; i < static_cast<int>(labels.size()) && i < start + 7; ++i)
            line(raster, i - start, wrapText(labels[i], 60).front(), cursor == i);
        line(raster, 8, std::to_string(notes.size()) + " notes | * = post-it | " + std::to_string(cursor + 1) + "/" + std::to_string(labels.size()));
    } else if (view == View::Network) {
        line(raster, 0, "RESEAU WI-FI PRIVE (LOCAL SEULEMENT)");
        line(raster, 2, ssid); line(raster, 3, "Mot de passe : " + password);
        line(raster, 5, "Puis ouvrir http://192.168.4.1");
        line(raster, 6, "Demander l'acces, puis accepter ici.");
        line(raster, 8, "Cle regeneree a chaque redemarrage.");
    } else if (view == View::Note) {
        line(raster, 0, wrapText((selected.pinned ? "* " : "") + selected.title, 60).front());
        const auto lines = wrapText(selected.text, 60);
        for (std::size_t i = page * 8; i < lines.size() && i < (page + 1) * 8; ++i) line(raster, i - page * 8 + 1, lines[i]);
        raster.text(20, 378, "Page " + std::to_string(page + 1) + "/" + std::to_string(std::max<std::size_t>(1, (lines.size() + 7) / 8)) + " | clic : actions", 2);
    } else if (view == View::Actions || view == View::Delete) {
        if (view == View::Delete) {
            line(raster, 0, "SUPPRIMER CETTE NOTE ?");
            line(raster, 2, "ANNULER", action == 0); line(raster, 3, "SUPPRIMER", action == 1);
        } else {
            const std::string labels[] = {selected.pinned ? "DESEPINGLER" : "EPINGLER POST-IT", "MODIFIER TEXTE", "SUPPRIMER", "RETOUR"};
            for (int i = 0; i < 4; ++i) line(raster, i + 1, labels[i], action == i);
        }
    } else if (view == View::Editor) {
        line(raster, 0, "BROUILLON | " + std::to_string(draft.size()) + "/4096 OCTETS");
        const auto lines = wrapText(draft, 60);
        const auto start = lines.size() > 3 ? lines.size() - 3 : 0;
        for (std::size_t i = start; i < lines.size(); ++i) line(raster, i - start + 1, lines[i]);
        const std::string commandsText[] = {"SAUVER", "ESPACE", "EFFACER", "LIGNE", "ANNULER"};
        for (int i = 0; i < commands; ++i) {
            const int x = 18 + i * 156;
            if (key == i) raster.bar(x - 4, 222, 150, 28);
            raster.text(x, 228, commandsText[i], 2, key == i);
        }
        for (std::size_t i = 0; i < alphabet.size(); ++i) {
            const int x = 22 + (i % 14) * 54, y = 270 + (i / 14) * 34;
            if (key == static_cast<int>(i) + commands) raster.bar(x - 5, y - 4, 32, 28);
            raster.text(x, y, std::string(1, alphabet[i]), 2, key == static_cast<int>(i) + commands);
        }
    }
    const auto notices = wrapText(message, 60);
    raster.text(20, 416, notices.front(), 2);
    raster.text(20, 448, "HAUT/BAS : CHOIX | CLIC : OK | BOOT : RETOUR", 2);
    std::string error;
    if (!display(frame, error)) { fault = error; Serial.println(error.c_str()); }
    else if (showingApproval) approvalDisplayed = true;
}
}
void setup() {
    Serial.begin(115200); delay(200);
    Serial.println("MON COMPAGNON ALPHA NOTES - not diagnostic");
    frame = static_cast<uint8_t*>(ps_malloc(48000));
    if (!frame) { Serial.println("STOP: framebuffer allocation failed"); return; }
    if (!beginBoard(fault)) { Serial.println(fault.c_str()); return; }
    if (!mountNotes(fault)) { Serial.println(fault.c_str()); render(); return; }
    files.reset(new PosixNoteFiles(kNotesDirectory)); store.reset(new NoteStore(*files));
    if (store->recover() != NoteResult::Ok) { fault = "Recuperation des notes impossible. Fichiers conserves."; Serial.println(fault.c_str()); render(); return; }
    notesApi.reset(new NoteHttp(*store));
    WiFi.mode(WIFI_AP);
    unsigned char random[16]; esp_fill_random(random, sizeof random);
    char name[32]; snprintf(name, sizeof name, "COMPAGNON-%02X%02X", random[0], random[1]); ssid = name;
    constexpr char chars[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    for (int i = 2; i < 14; ++i) password += chars[random[i] % (sizeof chars - 1)];
    if (!WiFi.softAP(ssid.c_str(), password.c_str(), 1, false, 1)) { fault = "Point d'acces Wi-Fi indisponible."; render(); return; }
    const std::string origin = "http://" + std::string(WiFi.softAPIP().toString().c_str());
    server.reset(new EmbeddedHttp(origin, notesApi.get()));
    if (server->begin() != ESP_OK) { fault = "Serveur web indisponible."; render(); return; }
    refreshNotes(); message = "Pret. Audio, Google et veilles absents de cette alpha.";
    Serial.println("READY alpha notes; SD mounted without format; web requires physical approval.");
    render(); dirty = false;
}
void loop() {
    if (!frame || !server || !fault.empty()) { delay(20); return; }
    const auto now = millis();
    const bool active = server->approved();
    if (active != webActive) { webActive = active; dirty = true; }
    if (Serial.available() && Serial.read() == 't') {
        // Explicit USB test: create/read back/remove only our temporary note.
        auto result = server->localNotes([](NoteService& service) {
            auto before = service.list();
            if (!before.ok()) return before;
            auto created = service.create({"Test temporaire alpha", "Verification SD alpha", std::nullopt});
            if (!created.ok()) return created;
            NoteStore reopened(*files);
            const auto recovered = reopened.recover();
            const Note* check = reopened.find(created.note->id);
            if (recovered != NoteResult::Ok || !check || check->text != "Verification SD alpha") {
                NoteServiceResponse failed; failed.result = NoteResult::IoError;
                failed.status = 503; failed.message = "Test lecture SD echoue. Note temporaire conservee."; return failed;
            }
            return service.erase(created.note->id, created.note->version);
        });
        Serial.println(result.ok() ? "SD_TEST PASS: create + fsync + recovery + readback + delete" : "SD_TEST FAIL: storage error; inspect files before retry");
        message = result.ok() ? "Test SD reussi : creation, relecture, suppression." : result.message;
        refreshNotes(); dirty = true;
    }
    const auto request = server->pendingRequestId();
    if (request != showingRequest) {
        showingRequest = request; showingApproval = request != 0;
        approvalDisplayed = false; approvalArmed = false; dirty = true;
    }
    if (showingApproval && approvalDisplayed && !approvalArmed &&
        digitalRead(4) == HIGH && digitalRead(5) == HIGH && digitalRead(6) == HIGH && digitalRead(0) == HIGH)
        approvalArmed = true;
    for (int i = 0; i < 4; ++i) if (buttons[i].pressed(now)) input(i);
    if (now - lastPoll >= 5000) { lastPoll = now; refreshNotes(); }
    if (dirty) { dirty = false; render(); }
    delay(5);
}
