// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace compagnon::device {
inline constexpr int kScreenWidth = 800;
inline constexpr int kScreenHeight = 480;
inline constexpr std::size_t kFrameBytes = 48000;
inline constexpr char kNotesDirectory[] = "/sdcard/compagnon-notes";
inline constexpr int kButtonUp = 4;
inline constexpr int kButtonSelect = 5;
inline constexpr int kButtonDown = 6;
inline constexpr int kButtonBoot = 0;

// Single owning task; do not call concurrently. No PMIC or charger writes.
bool beginBoard(std::string& error);
// Mount existing FAT without formatting; create only kNotesDirectory.
bool mountNotes(std::string& error);
// Exactly 48000 readable bytes, row-major 100 bytes/row, MSB left, 1=white.
// Synchronous normal full waveform; frame must remain stable until return.
bool display(const uint8_t* mono48000, std::string& error);
}  // namespace compagnon::device
