// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>

namespace compagnon::device {
// Synchronous real TCP loopback check; call from an application-owned worker.
// Four bounded GETs, no bootstrap, cookie, private data or session mutation.
// Summary contains only fixed route names, status, byte count and SDK errors.
bool runHttpSelfTest(std::string& summary);
}  // namespace compagnon::device
