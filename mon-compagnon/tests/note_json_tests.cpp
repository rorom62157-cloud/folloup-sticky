#include "note_json.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace compagnon;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)

void reject(const std::string& body, bool update = false) {
    NoteDraft draft{"kept title", "kept text", true, "kept_audio"};
    std::uint64_t version = 123;
    CHECK(!parseNoteJson(body, update, draft, version));
    CHECK(draft.title == "kept title" && draft.text == "kept text" &&
          draft.pinned && draft.audio_id == "kept_audio" && version == 123);
}
std::string create(const std::string& title, const std::string& text) {
    return "{\"title\":\"" + title + "\",\"text\":\"" + text + "\"}";
}
std::string patch(const std::string& version) {
    return "{\"title\":\"\",\"text\":\"\",\"pinned\":true,\"version\":" + version + "}";
}
int main() {
    NoteDraft draft;
    std::uint64_t version = 88;
    CHECK(parseNoteJson(" \n{\"text\":\"bonjour\",\"title\":\"été 🌍\"}\r\t", false, draft, version));
    CHECK(draft.title == "été 🌍" && draft.text == "bonjour" && !draft.pinned && version == 0);
    CHECK(parseNoteJson(create("", ""), false, draft, version));
    reject("{\"title\":\"x\",\"text\":\"y\",\"pinned\":true}");
    CHECK(parseNoteJson(patch("9007199254740991"), true, draft, version));
    CHECK(version == NoteStore::max_version && draft.pinned);
    CHECK(parseNoteJson("{\"title\":\"\\u00e9\\uD83C\\uDF0D\",\"text\":\"\\\"\\\\\\/\\t\\n\\r\"}", false, draft, version));
    CHECK(draft.title == "é🌍" && draft.text == "\"\\/\t\n\r");
    CHECK(parseNoteJson("{\"tit\\u006ce\":\"ok\",\"text\":\"ok\"}", false, draft, version));
    for (const std::string body : {
        "", "{}", "[]", "null", "true", "42", "{\"title\":\"x\"}",
        "{\"title\":null,\"text\":\"x\"}", "{\"title\":{},\"text\":\"x\"}",
        "{\"title\":[],\"text\":\"x\"}", "{\"title\":\"x\",\"text\":false}",
        "{\"title\":\"x\",\"text\":\"y\",}", "{\"title\":\"x\" \"text\":\"y\"}",
        "{\"title\":\"x\",\"text\":\"y\"}false", "{\"title\":\"x\",\"text\":\"y\"}{}",
        "{\"title\":\"x\",\"text\":\"y\",\"extra\":0}",
        "{\"title\":\"x\",\"text\":\"y\",\"version\":1}",
        "{\"title\":\"x\",\"text\":\"y\",\"audio_id\":\"abc\"}",
        "{\"title\":\"x\",\"text\":\"y\",\"pinned\":1}",
        "{\"title\":\"x\",\"text\":\"y\",\"pinned\":truefalse}",
        "{\"title\":\"x\",\"text\":\"y\",\"title\":\"z\"}",
        "{\"title\":\"x\",\"text\":\"y\",\"tit\\u006ce\":\"z\"}",
        "{\"title\":\"x\",\"text\":\"y\",\"pinned\":true,\"pinned\":false}"}) reject(body);
    reject(create("x", "y"), true);
    reject("{\"title\":\"x\",\"text\":\"y\",\"version\":1}", true);
    reject("{\"title\":\"x\",\"text\":\"y\",\"pinned\":true}", true);
    reject("{\"title\":\"x\",\"text\":\"y\",\"pinned\":true,\"version\":1,\"version\":2}", true);
    for (const std::string number : {"0", "-1", "+1", "01", "1.0", "1e0", "1E1", "NaN", "null", "true", "\"1\"", "9007199254740992", "18446744073709551615", "99999999999999999999999999999999"}) reject(patch(number), true);
    for (const std::string escape : {"\\u0000", "\\u0001", "\\b", "\\f", "\\u007f", "\\q", "\\x41", "\\u123", "\\uZZZZ", "\\ud800", "\\udfff", "\\ud800x", "\\ud800\\u0041", "\\ud800\\ud800", "\\udc00\\ud800"}) reject(create("x", escape));
    for (const std::string& bad : {std::string(1, '\0'), std::string(1, '\n'), std::string("\xc0\x80", 2), std::string("\xc1\xbf", 2), std::string("\x80", 1), std::string("\xe0\x80\x80", 3), std::string("\xed\xa0\x80", 3), std::string("\xf0\x80\x80\x80", 4), std::string("\xf4\x90\x80\x80", 4), std::string("\xf5\x80\x80\x80", 4), std::string("\xe2\x82", 2), std::string("\xe2\x28\xa1", 3)}) reject(create("x", bad));
    reject(std::string("\xef\xbb\xbf") + create("x", "y"));
    reject(create("x", "y") + std::string(1, '\0'));
    reject(std::string(10000, '['));
    CHECK(parseNoteJson(create(std::string(160, 'x'), std::string(4096, 'y')), false, draft, version));
    reject(create(std::string(161, 'x'), "y"));
    reject(create("x", std::string(4097, 'y')));
    std::string accented;
    for (int i = 0; i < 80; ++i) accented += "é";
    CHECK(parseNoteJson(create(accented, "y"), false, draft, version));
    reject(create(accented + "é", "y"));
    std::string escaped;
    for (int i = 0; i < 4096; ++i) escaped += "\\u0061";
    // The decoded text fits, but encoded overhead still exceeds the HTTP cap.
    reject(create("x", escaped));
    auto padded = create("x", "y"); padded.resize(max_note_json_bytes, ' ');
    CHECK(parseNoteJson(padded, false, draft, version));
    padded += ' '; reject(padded);
    for (const std::string good : {"\xc2\x80", "\xdf\xbf", "\xe0\xa0\x80", "\xef\xbf\xbf", "\xf0\x90\x80\x80", "\xf4\x8f\xbf\xbf"}) CHECK(parseNoteJson(create("x", good), false, draft, version));
    const Note note{"note_1", "titre \"🌍\"", "ligne\n\t\\/é\r", true, 42, "audio_2"};
    CHECK(noteJson(note) == "{\"id\":\"note_1\",\"title\":\"titre \\\"🌍\\\"\",\"text\":\"ligne\\u000a\\u0009\\\\/é\\u000d\",\"pinned\":true,\"version\":42,\"audio_id\":\"audio_2\"}");
    CHECK(notesJson({}) == "{\"notes\":[]}");
    CHECK(notesJson({note, note}) == "{\"notes\":[" + noteJson(note) + "," + noteJson(note) + "]}");
    Note plain = note; plain.audio_id.clear();
    CHECK(noteJson(plain).find("audio_id") == std::string::npos);
    // Roundtrip every store-accepted ASCII control and JSON delimiter via the
    // encoder's actual mutation fields, then every prefix to catch truncation.
    plain.title = "\"\\/\t\n\r é🌍"; plain.text = plain.title;
    auto encoded = noteJson(plain);
    encoded.erase(1, encoded.find(',') ); // Remove generated id field.
    CHECK(parseNoteJson(encoded, true, draft, version));
    CHECK(draft.title == plain.title && draft.text == plain.text && draft.pinned && version == plain.version);
    for (std::size_t i = 0; i < encoded.size(); ++i) reject(encoded.substr(0, i), true);
    std::cout << "note JSON tests passed\n";
}
