#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace compagnon {
struct FileEntry {
    std::string name;
    std::string relative_path;
    bool directory = false;
    std::uint64_t size = 0;
};

// Read-only view of an already-mounted SD. Caller serializes storage access and
// prevents concurrent directory replacement/unmount. Host symlink checks do not
// constitute a sandbox against concurrent external filesystem writers.
class LocalFiles {
public:
    static constexpr std::size_t max_entries = 64;
    static constexpr std::size_t max_text_bytes = 65536;
    explicit LocalFiles(std::string absoluteRoot = "/sdcard");
    bool list(const std::string& relativeDir, std::vector<FileEntry>& entries,
              std::string& error) const;
    bool readText(const std::string& relativePath, std::string& text,
                  std::string& error) const;
private:
    bool resolve(const std::string& relative, std::string& absolute,
                 std::string& error) const;
    std::string root_;
};
} // namespace compagnon
