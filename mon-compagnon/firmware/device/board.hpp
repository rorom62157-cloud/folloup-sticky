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
// Synchronous; frame must remain stable until return. Normal F7 establishes or
// explicitly cleans the glass. fast=true uses verified old/new differential FF
// without reset after a successful base; identical frames are skipped. Both RAM
// planes cover the full panel (RAM windows do not bound physical drive).
// Errors invalidate the retained shadow; the next call restores a normal base.
bool display(const uint8_t* mono48000, std::string& error, bool fast = false);
}  // namespace compagnon::device
