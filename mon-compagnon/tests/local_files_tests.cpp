#include "local_files.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <unistd.h>

using namespace compagnon;
namespace fs = std::filesystem;
namespace {
struct Temp {
    std::string path;
    Temp() {
        char pattern[] = "/tmp/compagnon-reader-XXXXXX";
        const auto* created = ::mkdtemp(pattern);
        assert(created);
        char* physical = ::realpath(created, nullptr);
        assert(physical);
        path = physical;
        std::free(physical);
    }
    ~Temp() { fs::remove_all(path); }
};
void put(const std::string& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    file << bytes;
    file.close();
    assert(file);
}
std::map<std::string, std::string> snapshot(const std::string& path) {
    std::map<std::string, std::string> result;
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        const auto name = entry.path().lexically_relative(path).string();
        if (entry.is_symlink()) result[name] = "link:" + fs::read_symlink(entry.path()).string();
        else if (entry.is_directory()) result[name] = "directory";
        else {
            std::ifstream file(entry.path(), std::ios::binary);
            result[name] = "file:" + std::string(std::istreambuf_iterator<char>(file), {});
        }
    }
    return result;
}
}
int main() {
    Temp temp;
    const auto root = temp.path + "/sd";
    fs::create_directories(root + "/documents/nested");
    fs::create_directories(root + "/compagnon-notes");
    fs::create_directories(root + "/compagnon-tasks");
    fs::create_directories(root + "/.hidden");
    put(root + "/hello.txt", "Bonjour, été\n\tFin\r\n");
    put(root + "/empty.MD", "");
    put(root + "/documents/readme.md", "# Titre\n");
    put(root + "/documents/nested/long.txt", std::string(65536, 'a'));
    put(root + "/large.txt", std::string(65537, 'z'));
    put(root + "/music.wav", "audio");
    put(root + "/book.epub", "book");
    put(root + "/compagnon-notes/secret.txt", "private");
    put(root + "/compagnon-tasks/secret.txt", "private");
    put(root + "/.hidden/secret.txt", "private");
    put(root + "/.secret.txt", "private");
    assert(::symlink((root + "/hello.txt").c_str(), (root + "/alias.txt").c_str()) == 0);
    assert(::symlink((root + "/documents").c_str(), (root + "/aliasdir").c_str()) == 0);
    const auto before = snapshot(root);
    LocalFiles files(root);
    std::vector<FileEntry> entries;
    std::string error, text;
    assert(files.list("", entries, error) && error.empty());
    assert(entries.size() == 4 && entries[0].directory && entries[0].name == "documents");
    for (const auto& entry : entries) {
        assert(entry.relative_path == entry.name);
        if (entry.name == "hello.txt") assert(!entry.directory && entry.size == std::string("Bonjour, été\n\tFin\r\n").size());
    }
    assert(files.list("documents", entries, error) && entries.size() == 2);
    assert(entries[0].relative_path == "documents/nested" && entries[0].directory);
    assert(files.readText("hello.txt", text, error) && text == "Bonjour, été\n\tFin\r\n");
    assert(files.readText("empty.MD", text, error) && text.empty());
    assert(files.readText("documents/nested/long.txt", text, error) && text.size() == 65536);
    assert(!files.readText("large.txt", text, error) && text.empty() && !error.empty());
    for (const auto& path : {"../hello.txt", "/hello.txt", "documents/../hello.txt", "documents//readme.md",
                             "documents/", "./hello.txt", "compagnon-notes/secret.txt", "COMPAGNON-NOTES/secret.txt",
                             "compagnon-tasks/secret.txt", ".hidden/secret.txt", "alias.txt", "aliasdir/readme.md",
                             "music.wav", "book.epub", "missing.txt", "documents\\readme.md", "documents/readme.md "}) {
        text = "old";
        assert(!files.readText(path, text, error) && text.empty() && !error.empty());
    }
    assert(!files.readText(std::string("hello.txt\0x", 11), text, error));
    assert(!files.list("aliasdir", entries, error) && entries.empty());
    assert(!files.list("hello.txt", entries, error) && entries.empty());
    assert(!files.list("../", entries, error) && entries.empty());
    assert(!LocalFiles(temp.path + "/sd/../sd").list("", entries, error));
    assert(!LocalFiles("relative").list("", entries, error));
    assert(::symlink(root.c_str(), (temp.path + "/rootlink").c_str()) == 0);
    assert(!LocalFiles(temp.path + "/rootlink").list("", entries, error));
    assert(snapshot(root) == before); // All browser/reader operations preserved original files.

    for (const auto& bytes : {std::string("a\0b", 3), std::string("\xc0\xaf", 2),
                              std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4),
                              std::string("\xe2\x82", 2), std::string("\x80", 1),
                              std::string("\x1b", 1), std::string("\x7f", 1), std::string("\xc2\x85", 2)}) {
        put(root + "/invalid.txt", bytes);
        assert(!files.readText("invalid.txt", text, error) && text.empty() && !error.empty());
    }
    put(root + "/valid.txt", "A😀\xef\xbb\xbf\n");
    assert(files.readText("valid.txt", text, error));
    fs::create_directory(root + "/many");
    for (int i = 0; i < 64; ++i) put(root + "/many/" + std::to_string(i) + ".txt", "");
    assert(files.list("many", entries, error) && entries.size() == 64);
    put(root + "/many/overflow.md", "");
    assert(!files.list("many", entries, error) && entries.empty() && error.find("64") != std::string::npos);
    fs::create_directory(root + "/unsupported");
    for (int i = 0; i < 4097; ++i) put(root + "/unsupported/" + std::to_string(i) + ".bin", "");
    assert(!files.list("unsupported", entries, error) && entries.empty() && error.find("4096") != std::string::npos);
    fs::remove_all(root);
    assert(!files.list("", entries, error) && entries.empty());
    assert(!files.readText("hello.txt", text, error) && text.empty());
    std::cout << "Local SD read-only browser tests passed\n";
}
