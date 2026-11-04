#include "../firmware/device/raster.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <iostream>

using compagnon::device::Raster;
using compagnon::device::wrapText;

int main() {
    std::array<std::uint8_t, Raster::bytes + 2> guarded{};
    guarded.front() = 0xA5;
    guarded.back() = 0x5A;
    Raster raster(guarded.data() + 1);
    raster.clear();
    assert(raster.data() == guarded.data() + 1);
    assert(std::all_of(raster.data(), raster.data() + Raster::bytes,
                       [](auto byte) { return byte == 0xFF; }));
    raster.bar(0, 0, 1, 1);
    assert(raster.data()[0] == 0x7F);
    raster.bar(7, 0, 2, 1);
    assert(raster.data()[0] == 0x7E && raster.data()[1] == 0x7F);
    raster.bar(0, 0, 1, 1, false);
    assert(raster.data()[0] == 0xFE);
    raster.bar(799, 479, 100, 100);
    assert(raster.data()[Raster::bytes - 1] == 0xFE);
    raster.clear();
    raster.text(0, 0, "A", 1);
    assert(raster.data()[0] == 0x8F); // 01110 followed by three white pixels.
    assert(raster.data()[100] == 0x77); // 10001.
    assert(raster.data()[700] == 0xFF); // eighth row cell spacing.
    const auto normal = guarded;
    raster.clear();
    raster.text(0, 0, "à", 1);
    assert(guarded == normal);
    raster.clear();
    raster.text(0, 0, "A", 1, true);
    assert(raster.data()[0] == 0x73); // inverted six-pixel cell, two white outside.
    assert(raster.data()[700] == 0x03);
    raster.clear();
    raster.text(0, 0, "a\na", 1);
    assert(raster.data()[0] == raster.data()[800]);
    assert(raster.data()[700] == 0xFF);

    // Exercise every canvas edge, negative dimensions, and huge coordinates.
    raster.bar(-10, -10, 20, 20);
    raster.bar(INT_MIN, INT_MIN, INT_MAX, INT_MAX);
    raster.bar(INT_MAX, INT_MAX, INT_MAX, INT_MAX);
    raster.bar(0, 0, -1, -1);
    raster.text(-15, -15, "Écrêtage ?", 3);
    raster.text(798, 478, "AB", 3, true);
    raster.text(INT_MIN, INT_MIN, "A", INT_MAX);
    raster.text(INT_MAX, INT_MAX, "A\nB", INT_MAX);
    raster.text(0, 0, "A", 0);
    assert(guarded.front() == 0xA5 && guarded.back() == 0x5A);
    Raster null(nullptr);
    null.clear(); null.bar(0, 0, 1, 1); null.text(0, 0, "safe");

    assert((wrapText("été déjà Noël, cœur", 80) ==
            std::vector<std::string>{"ETE DEJA NOEL, COEUR"}));
    assert((wrapText("un deux trois", 6) ==
            std::vector<std::string>{"UN", "DEUX", "TROIS"}));
    assert((wrapText("abcdefgh", 3) ==
            std::vector<std::string>{"ABC", "DEF", "GH"}));
    assert((wrapText("a\n\nb\n", 4) ==
            std::vector<std::string>{"A", "", "B", ""}));
    assert((wrapText("", 4) == std::vector<std::string>{""}));
    assert((wrapText("a😀b漢", 20) == std::vector<std::string>{"A?B?"}));
    assert((wrapText("e\xCC\x81", 20) == std::vector<std::string>{"E?"}));
    assert((wrapText("a\0b", 20) == std::vector<std::string>{"A"}));
    assert((wrapText(std::string("a\0b", 3), 20) == std::vector<std::string>{"A?B"}));
    assert((wrapText("ab", 0) == std::vector<std::string>{"A", "B"}));
    assert((wrapText("\xF0\x80\x80\x80", 8) == std::vector<std::string>{"?"}));
    assert((wrapText("\xC3X", 8) == std::vector<std::string>{"?X"}));
    assert((wrapText("ÀÂÄÇÉÈÊËÎÏÔÖÙÛÜŸŒÆ", 80) ==
            std::vector<std::string>{"AAACEEEEIIOOUUUYOEAE"}));
    const std::string original = "écriture 😀";
    (void)wrapText(original, 4);
    assert(original == "écriture 😀");
    std::cout << "raster tests passed\n";
}
