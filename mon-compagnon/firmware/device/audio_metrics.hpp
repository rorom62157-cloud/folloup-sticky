#pragma once
#include <string>
namespace compagnon::device {
// Explicit local diagnostic; bounded finalized own recording only. Summary
// contains aggregate statistics, never samples, file content or a path.
bool audioDiagnostics(const std::string& path,std::string& summary);
}
