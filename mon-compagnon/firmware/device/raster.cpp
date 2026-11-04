#include "raster.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace compagnon::device {
namespace {
// Each row is five pixels, bit 4 at the left. These small geometric patterns
// were authored for this application rather than imported from a font package.
using Glyph = std::array<std::uint8_t, 7>;
Glyph glyph(char c) {
    switch (c) {
    case 'A': return {14,17,17,31,17,17,17};
    case 'B': return {30,17,17,30,17,17,30};
    case 'C': return {14,17,16,16,16,17,14};
    case 'D': return {30,17,17,17,17,17,30};
    case 'E': return {31,16,16,30,16,16,31};
    case 'F': return {31,16,16,30,16,16,16};
    case 'G': return {14,17,16,23,17,17,15};
    case 'H': return {17,17,17,31,17,17,17};
    case 'I': return {14,4,4,4,4,4,14};
    case 'J': return {7,2,2,2,18,18,12};
    case 'K': return {17,18,20,24,20,18,17};
    case 'L': return {16,16,16,16,16,16,31};
    case 'M': return {17,27,21,21,17,17,17};
    case 'N': return {17,25,21,19,17,17,17};
    case 'O': return {14,17,17,17,17,17,14};
    case 'P': return {30,17,17,30,16,16,16};
    case 'Q': return {14,17,17,17,21,18,13};
    case 'R': return {30,17,17,30,20,18,17};
    case 'S': return {15,16,16,14,1,1,30};
    case 'T': return {31,4,4,4,4,4,4};
    case 'U': return {17,17,17,17,17,17,14};
    case 'V': return {17,17,17,17,17,10,4};
    case 'W': return {17,17,17,21,21,21,10};
    case 'X': return {17,17,10,4,10,17,17};
    case 'Y': return {17,17,10,4,4,4,4};
    case 'Z': return {31,1,2,4,8,16,31};
    case '0': return {14,17,19,21,25,17,14};
    case '1': return {4,12,4,4,4,4,14};
    case '2': return {14,17,1,2,4,8,31};
    case '3': return {30,1,1,14,1,1,30};
    case '4': return {2,6,10,18,31,2,2};
    case '5': return {31,16,16,30,1,1,30};
    case '6': return {14,16,16,30,17,17,14};
    case '7': return {31,1,2,4,8,8,8};
    case '8': return {14,17,17,14,17,17,14};
    case '9': return {14,17,17,15,1,1,14};
    case ' ': return {0,0,0,0,0,0,0};
    case '.': return {0,0,0,0,0,6,6};
    case ',': return {0,0,0,0,6,6,4};
    case ':': return {0,6,6,0,6,6,0};
    case ';': return {0,6,6,0,6,6,4};
    case '!': return {4,4,4,4,4,0,4};
    case '?': return {14,17,1,2,4,0,4};
    case '-': return {0,0,0,31,0,0,0};
    case '_': return {0,0,0,0,0,0,31};
    case '+': return {0,4,4,31,4,4,0};
    case '=': return {0,0,31,0,31,0,0};
    case '/': return {1,2,2,4,8,8,16};
    case '\\': return {16,8,8,4,2,2,1};
    case '(': return {2,4,8,8,8,4,2};
    case ')': return {8,4,2,2,2,4,8};
    case '[': return {14,8,8,8,8,8,14};
    case ']': return {14,2,2,2,2,2,14};
    case '<': return {0,2,4,8,4,2,0};
    case '>': return {0,8,4,2,4,8,0};
    case '\'': return {4,4,8,0,0,0,0};
    case '"': return {10,10,10,0,0,0,0};
    case '%': return {17,2,4,4,8,16,17};
    case '#': return {10,10,31,10,31,10,10};
    case '*': return {0,21,14,31,14,21,0};
    case '@': return {14,17,23,21,23,16,14};
    case '&': return {12,18,20,8,21,18,13};
    case '|': return {4,4,4,4,4,4,4};
    default: return {14,17,1,2,4,0,4};
    }
}

std::uint32_t next(const std::string& value, std::size_t& i) {
    const auto first = static_cast<unsigned char>(value[i++]);
    if (first < 128) return first;
    int extra = 0;
    std::uint32_t cp = 0, minimum = 0;
    if (first >= 0xC2 && first <= 0xDF) { extra = 1; cp = first & 31; minimum = 0x80; }
    else if (first >= 0xE0 && first <= 0xEF) { extra = 2; cp = first & 15; minimum = 0x800; }
    else if (first >= 0xF0 && first <= 0xF4) { extra = 3; cp = first & 7; minimum = 0x10000; }
    else return 0xFFFD;
    const std::size_t start = i;
    for (int j = 0; j < extra; ++j) {
        if (i == value.size() || (static_cast<unsigned char>(value[i]) & 0xC0) != 0x80) {
            i = start;
            return 0xFFFD;
        }
        cp = (cp << 6) | (static_cast<unsigned char>(value[i++]) & 63);
    }
    return cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) ? 0xFFFD : cp;
}

std::string displayText(const std::string& value) {
    std::string result;
    for (std::size_t i = 0; i < value.size();) {
        const auto c = next(value, i);
        if (c >= 'a' && c <= 'z') result += static_cast<char>(c - 'a' + 'A');
        else if (c == '\n' || c == '\t' || (c >= 32 && c <= 126)) {
            // All printable ASCII without a supported drawing maps visibly to ?.
            if (c == '\t') result += ' ';
            else if (c == '$' || c == '^' || c == '`' || c == '{' || c == '}' || c == '~') result += '?';
            else result += static_cast<char>(c);
        } else if ((c >= 0xC0 && c <= 0xC5) || (c >= 0xE0 && c <= 0xE5)) result += 'A';
        else if (c == 0xC6 || c == 0xE6) result += "AE";
        else if (c == 0xC7 || c == 0xE7) result += 'C';
        else if ((c >= 0xC8 && c <= 0xCB) || (c >= 0xE8 && c <= 0xEB)) result += 'E';
        else if ((c >= 0xCC && c <= 0xCF) || (c >= 0xEC && c <= 0xEF)) result += 'I';
        else if (c == 0xD1 || c == 0xF1) result += 'N';
        else if ((c >= 0xD2 && c <= 0xD6) || (c >= 0xF2 && c <= 0xF6)) result += 'O';
        else if ((c >= 0xD9 && c <= 0xDC) || (c >= 0xF9 && c <= 0xFC)) result += 'U';
        else if (c == 0xDD || c == 0xFD || c == 0xFF || c == 0x178) result += 'Y';
        else if (c == 0x152 || c == 0x153) result += "OE";
        else if (c == 0x2019 || c == 0x2018) result += '\'';
        else if (c == 0x201C || c == 0x201D || c == 0xAB || c == 0xBB) result += '"';
        else if (c == 0x2013 || c == 0x2014) result += '-';
        else if (c == 0x2026) result += "...";
        else if (c == 0xA0) result += ' ';
        else result += '?';
    }
    return result;
}
} // namespace

std::vector<std::string> wrapText(const std::string& value, std::size_t columns) {
    const auto normalized = displayText(value);
    columns = std::max<std::size_t>(columns, 1);
    std::vector<std::string> lines;
    std::size_t begin = 0;
    do {
        const auto end = normalized.find('\n', begin);
        auto line = normalized.substr(begin, end == std::string::npos ? end : end - begin);
        while (line.size() > columns) {
            auto cut = line.rfind(' ', columns);
            if (cut == std::string::npos || cut == 0) cut = columns;
            lines.push_back(line.substr(0, cut));
            auto resume = cut;
            while (resume < line.size() && line[resume] == ' ') ++resume;
            line.erase(0, resume);
        }
        lines.push_back(line);
        if (end == std::string::npos) break;
        begin = end + 1;
    } while (true);
    return lines;
}

Raster::Raster(std::uint8_t* buffer) : buffer_(buffer) {}
void Raster::clear() { if (buffer_) std::memset(buffer_, 0xFF, bytes); }
const std::uint8_t* Raster::data() const { return buffer_; }
void Raster::pixel(std::int64_t x, std::int64_t y, bool black) {
    if (!buffer_ || x < 0 || y < 0 || x >= width || y >= height) return;
    const auto offset = static_cast<std::size_t>(y * (width / 8) + x / 8);
    const auto bit = static_cast<std::uint8_t>(0x80 >> (x % 8));
    if (black) buffer_[offset] &= static_cast<std::uint8_t>(~bit);
    else buffer_[offset] |= bit;
}
void Raster::bar(int x, int y, int w, int h, bool black) {
    if (!buffer_ || w <= 0 || h <= 0) return;
    const auto right = std::min<std::int64_t>(width, static_cast<std::int64_t>(x) + w);
    const auto bottom = std::min<std::int64_t>(height, static_cast<std::int64_t>(y) + h);
    for (auto yy = std::max<std::int64_t>(0, y); yy < bottom; ++yy)
        for (auto xx = std::max<std::int64_t>(0, x); xx < right; ++xx) pixel(xx, yy, black);
}
void Raster::text(int x, int y, const std::string& value, int scale, bool inverse) {
    if (!buffer_ || scale <= 0) return;
    std::int64_t cursor = x, top = y;
    const std::int64_t cellW = 6LL * scale, cellH = 8LL * scale;
    for (char c : displayText(value)) {
        if (c == '\n') { cursor = x; top += cellH; continue; }
        if (cursor < width && cursor + cellW > 0 && top < height && top + cellH > 0) {
            const auto rows = glyph(c);
            // Iterate clipped screen pixels, so even INT_MAX scale is bounded.
            for (auto yy = std::max<std::int64_t>(0, top); yy < std::min<std::int64_t>(height, top + cellH); ++yy)
                for (auto xx = std::max<std::int64_t>(0, cursor); xx < std::min<std::int64_t>(width, cursor + cellW); ++xx) {
                    const auto row = (yy - top) / scale, col = (xx - cursor) / scale;
                    const bool ink = row < 7 && col < 5 && (rows[static_cast<std::size_t>(row)] & (16 >> col));
                    pixel(xx, yy, inverse ? !ink : ink);
                }
        }
        cursor += cellW;
    }
}
} // namespace compagnon::device
