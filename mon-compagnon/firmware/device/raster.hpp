#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace compagnon::device {

// Display-only transliteration: ASCII lowercase becomes uppercase; common French
// accented Latin letters become base letters (OE/AE ligatures expand). Other
// Unicode scalar values, including combining marks, are visible '?'. Originals
// must remain in storage unchanged. This is deliberately not a full Unicode font.
// Explicit newlines (including empty lines) are retained. Long words split;
// spaces at a wrap boundary are consumed. A zero column count means one column.
// No paging/truncation: the caller chooses which returned lines to display.
std::vector<std::string> wrapText(const std::string& text, std::size_t columns);

class Raster {
public:
    static constexpr int width = 800;
    static constexpr int height = 480;
    static constexpr std::size_t bytes = 48000;
    // Buffer is borrowed, must have bytes capacity, and outlive this object.
    // A null buffer is permitted and makes drawing a no-op.
    explicit Raster(std::uint8_t* buffer);
    void clear();
    // 5x7 authored bitmap glyphs in 6x8 cells; inverse fills the cell black.
    // Coordinates clip to the canvas. Nonpositive scale draws nothing.
    void text(int x, int y, const std::string& value, int scale = 2,
              bool inverse = false);
    void bar(int x, int y, int w, int h, bool black = true);
    const std::uint8_t* data() const;
private:
    void pixel(std::int64_t x, std::int64_t y, bool black);
    std::uint8_t* buffer_;
};

} // namespace compagnon::device
