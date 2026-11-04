#include "note_json.hpp"

#include <utility>

namespace compagnon {
namespace {
void utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
        out.push_back(static_cast<char>(0x80 | (cp & 63)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
        out.push_back(static_cast<char>(0x80 | (cp & 63)));
    }
}
bool textCodepoint(std::uint32_t cp) {
    return cp != 127 && (cp >= 32 || cp == 9 || cp == 10 || cp == 13);
}
class Parser {
public:
    explicit Parser(const std::string& input) : in(input) {}
    void space() {
        while (pos < in.size() && (in[pos] == ' ' || in[pos] == '\t' ||
               in[pos] == '\r' || in[pos] == '\n')) ++pos;
    }
    bool take(char c) {
        space();
        if (pos == in.size() || in[pos] != c) return false;
        ++pos; return true;
    }
    bool end() { space(); return pos == in.size(); }
    bool string(std::string& out, std::size_t limit) {
        if (!take('"')) return false;
        while (pos < in.size()) {
            const auto c = static_cast<unsigned char>(in[pos++]);
            if (c == '"') return true;
            std::uint32_t cp = c;
            if (c == '\\') {
                if (pos == in.size()) return false;
                const char escape = in[pos++];
                switch (escape) {
                case '"': cp = '"'; break;
                case '\\': cp = '\\'; break;
                case '/': cp = '/'; break;
                case 'b': cp = 8; break;
                case 'f': cp = 12; break;
                case 'n': cp = 10; break;
                case 'r': cp = 13; break;
                case 't': cp = 9; break;
                case 'u': {
                    if (!hex4(cp)) return false;
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (in.size() - pos < 2 || in[pos++] != '\\' || in[pos++] != 'u') return false;
                        std::uint32_t low;
                        if (!hex4(low) || low < 0xdc00 || low > 0xdfff) return false;
                        cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
                    break;
                }
                default: return false;
                }
            } else if (c < 32) return false;
            else if (c >= 128) {
                unsigned count;
                std::uint32_t minimum;
                if (c >= 0xc2 && c <= 0xdf) { count = 1; cp = c & 31; minimum = 0x80; }
                else if (c >= 0xe0 && c <= 0xef) { count = 2; cp = c & 15; minimum = 0x800; }
                else if (c >= 0xf0 && c <= 0xf4) { count = 3; cp = c & 7; minimum = 0x10000; }
                else return false;
                if (in.size() - pos < count) return false;
                while (count--) {
                    const auto next = static_cast<unsigned char>(in[pos++]);
                    if ((next & 0xc0) != 0x80) return false;
                    cp = (cp << 6) | (next & 63);
                }
                if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
            }
            if (!textCodepoint(cp)) return false;
            utf8(out, cp);
            if (out.size() > limit) return false;
        }
        return false;
    }
    bool boolean(bool& value) {
        space();
        if (in.compare(pos, 4, "true") == 0) { pos += 4; value = true; return true; }
        if (in.compare(pos, 5, "false") == 0) { pos += 5; value = false; return true; }
        return false;
    }
    bool version(std::uint64_t& value) {
        space();
        if (pos == in.size() || in[pos] < '1' || in[pos] > '9') return false;
        value = 0;
        while (pos < in.size() && in[pos] >= '0' && in[pos] <= '9') {
            const unsigned digit = static_cast<unsigned>(in[pos++] - '0');
            if (value > (NoteStore::max_version - digit) / 10) return false;
            value = value * 10 + digit;
        }
        return true;
    }
private:
    bool hex4(std::uint32_t& value) {
        if (in.size() - pos < 4) return false;
        value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const char c = in[pos++];
            unsigned digit;
            if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
            else return false;
            value = value * 16 + digit;
        }
        return true;
    }
    const std::string& in;
    std::size_t pos = 0;
};
std::string quoted(const std::string& value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(static_cast<char>(c)); }
        else if (c < 32 || c == 127) {
            out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]);
        } else out.push_back(static_cast<char>(c));
    }
    out.push_back('"'); return out;
}
} // namespace

bool parseNoteJson(const std::string& body, bool update, NoteDraft& draft,
                   std::uint64_t& version) {
    if (body.size() > max_note_json_bytes) return false;
    Parser parser(body);
    NoteDraft candidate;
    std::uint64_t revision = 0;
    unsigned seen = 0;
    if (!parser.take('{')) return false;
    do {
        std::string key;
        if (!parser.string(key, 16) || !parser.take(':')) return false;
        unsigned bit;
        if (key == "title") bit = 1;
        else if (key == "text") bit = 2;
        else if (key == "pinned" && update) bit = 4;
        else if (key == "version" && update) bit = 8;
        else return false;
        if ((seen & bit) != 0) return false;
        seen |= bit;
        if (bit == 1 && !parser.string(candidate.title, NoteStore::max_title_bytes)) return false;
        if (bit == 2 && !parser.string(candidate.text, NoteStore::max_text_bytes)) return false;
        if (bit == 4 && !parser.boolean(candidate.pinned)) return false;
        if (bit == 8 && !parser.version(revision)) return false;
    } while (parser.take(','));
    if (!parser.take('}') || !parser.end() || (seen & 3) != 3 ||
        (update && seen != 15)) return false;
    draft = std::move(candidate); version = revision;
    return true;
}
std::string noteJson(const Note& note) {
    std::string out = "{\"id\":" + quoted(note.id) + ",\"title\":" + quoted(note.title) +
        ",\"text\":" + quoted(note.text) + ",\"pinned\":" + (note.pinned ? "true" : "false") +
        ",\"version\":" + std::to_string(note.version);
    if (!note.audio_id.empty()) out += ",\"audio_id\":" + quoted(note.audio_id);
    return out + "}";
}
std::string notesJson(const std::vector<Note>& notes) {
    std::string out = "{\"notes\":[";
    bool first = true;
    for (const auto& note : notes) {
        if (!first) out += ',';
        first = false; out += noteJson(note);
    }
    return out + "]}";
}
} // namespace compagnon
