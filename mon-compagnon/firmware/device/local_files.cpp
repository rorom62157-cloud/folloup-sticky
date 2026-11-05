#include "local_files.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <utility>

namespace compagnon {
namespace {
bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}
std::string lower(std::string value) {
    for (char& ch : value) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return value;
}
bool supported(const std::string& name) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    const auto ext = lower(name.substr(dot));
    return ext == ".txt" || ext == ".md";
}
bool textValid(const std::string& bytes) {
    for (std::size_t i = 0; i < bytes.size();) {
        const auto first = static_cast<unsigned char>(bytes[i++]);
        std::uint32_t cp = first;
        unsigned remaining = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { cp = first & 31; remaining = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { cp = first & 15; remaining = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { cp = first & 7; remaining = 3; minimum = 0x10000; }
        else if (first >= 0x80) return false;
        if (remaining > bytes.size() - i) return false;
        for (unsigned j = 0; j < remaining; ++j) {
            const auto next = static_cast<unsigned char>(bytes[i++]);
            if ((next & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (next & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        if ((cp < 32 && cp != 9 && cp != 10 && cp != 13) || (cp >= 0x7f && cp <= 0x9f)) return false;
    }
    return true;
}
bool componentValid(const std::string& part) {
    return !part.empty() && part.size() <= 255 && part.front() != '.' &&
           part.find('\\') == std::string::npos && part.find(':') == std::string::npos &&
           part.back() != ' ' && part.back() != '.' && textValid(part);
}
bool privateName(const std::string& part) {
    const auto name = lower(part);
    return name.rfind("compagnon-",0)==0;
}
int inspect(const std::string& path, struct stat& info) {
#ifdef ESP_PLATFORM
    return ::stat(path.c_str(), &info); // FAT has no symlinks.
#else
    return ::lstat(path.c_str(), &info);
#endif
}
bool directoryPath(const std::string& path) {
    struct stat info {};
    return inspect(path, info) == 0 && S_ISDIR(info.st_mode);
}
} // namespace

LocalFiles::LocalFiles(std::string absoluteRoot) : root_(std::move(absoluteRoot)) {}

bool LocalFiles::resolve(const std::string& relative, std::string& absolute,
                         std::string& error) const {
    error.clear();
    if (root_.empty() || root_.front() != '/' || root_.size() > 1024 ||
        root_.find('\0') != std::string::npos) return fail(error, "Racine SD invalide.");
    std::string base;
    for (std::size_t start = 1; start < root_.size();) {
        const auto end = root_.find('/', start);
        const auto part = root_.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == "..") return fail(error, "Racine SD invalide.");
        base += '/' + part;
        if (!directoryPath(base)) return fail(error, "Carte SD ou dossier indisponible.");
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (base.empty()) base = "/";
    if (!directoryPath(base)) return fail(error, "Carte SD indisponible.");
    if (relative.size() > 1024 || (!relative.empty() && relative.front() == '/'))
        return fail(error, "Chemin relatif invalide.");
    absolute = base;
    bool first = true;
    for (std::size_t start = 0; start < relative.size();) {
        const auto end = relative.find('/', start);
        const auto part = relative.substr(start, end == std::string::npos ? end : end - start);
        if (!componentValid(part) || (first && privateName(part)))
            return fail(error, "Chemin interdit.");
        if (absolute.back() != '/') absolute += '/';
        absolute += part;
        struct stat info {};
        if (inspect(absolute, info) != 0 || (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)))
            return fail(error, "Fichier ou dossier indisponible.");
        if (end == std::string::npos) break;
        if (!S_ISDIR(info.st_mode) || end + 1 == relative.size()) return fail(error, "Chemin invalide.");
        first = false;
        start = end + 1;
    }
    return true;
}

bool LocalFiles::list(const std::string& relativeDir, std::vector<FileEntry>& entries,
                      std::string& error) const {
    entries.clear();
    std::string path;
    if (!resolve(relativeDir, path, error)) return false;
    if (!directoryPath(path)) return fail(error, "Ce chemin n'est pas un dossier.");
    DIR* dir = ::opendir(path.c_str());
    if (!dir) return fail(error, "Lecture du dossier impossible.");
    std::vector<FileEntry> found;
    bool ok = true;
    std::size_t scanned = 0;
    for (;;) {
        errno = 0;
        const auto* item = ::readdir(dir);
        if (!item) { if (errno) ok = fail(error, "Lecture du dossier interrompue."); break; }
        if (++scanned > 4096) { ok = fail(error, "Dossier trop volumineux (4096 entrees maximum)."); break; }
        const std::string name = item->d_name;
        if (!componentValid(name) || (relativeDir.empty() && privateName(name))) continue;
        struct stat info {};
        if (inspect(path + '/' + name, info) != 0) { ok = fail(error, "Fichier devenu indisponible."); break; }
        if (!S_ISDIR(info.st_mode) && !(S_ISREG(info.st_mode) && supported(name))) continue;
        if (found.size() == max_entries) { ok = fail(error, "Dossier trop volumineux (64 entrees maximum)."); break; }
        if (info.st_size < 0) { ok = fail(error, "Taille de fichier invalide."); break; }
        found.push_back({name, relativeDir.empty() ? name : relativeDir + '/' + name,
                         S_ISDIR(info.st_mode), S_ISDIR(info.st_mode) ? 0 : static_cast<std::uint64_t>(info.st_size)});
    }
    if (::closedir(dir) != 0) ok = fail(error, "Fermeture du dossier impossible.");
    if (!ok) return false;
    std::sort(found.begin(), found.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.directory != b.directory) return a.directory;
        return a.name < b.name;
    });
    entries = std::move(found);
    return true;
}

bool LocalFiles::readText(const std::string& relativePath, std::string& text,
                          std::string& error) const {
    text.clear();
    std::string path;
    if (!resolve(relativePath, path, error)) return false;
    struct stat info {};
    if (!supported(relativePath) || inspect(path, info) != 0 || !S_ISREG(info.st_mode))
        return fail(error, "Format non pris en charge (TXT ou MD).");
    if (info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) > max_text_bytes)
        return fail(error, "Texte trop volumineux (64 Kio maximum).");
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return fail(error, "Ouverture du texte impossible.");
    std::string bytes(static_cast<std::size_t>(info.st_size), '\0');
    const auto count = bytes.empty() ? 0 : std::fread(&bytes[0], 1, bytes.size(), file);
    const bool good = count == bytes.size() && std::fgetc(file) == EOF && !std::ferror(file);
    const bool closed = std::fclose(file) == 0;
    if (!good || !closed) return fail(error, "Lecture du texte interrompue ou fichier modifie.");
    if (!textValid(bytes)) return fail(error, "Texte UTF-8 invalide ou contenu binaire.");
    text = std::move(bytes);
    return true;
}
} // namespace compagnon
