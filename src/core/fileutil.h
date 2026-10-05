// Small file-system helpers. Paths are UTF-8 everywhere in the mod and are
// converted to UTF-16 only at the Win32 API boundary.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace omm::fs {

struct DirEntry {
    std::string name;   // file name only
    std::string path;   // full path
    bool isDir = false;
    uint64_t size = 0;
};

bool Exists(const std::string& path);
bool IsDirectory(const std::string& path);
bool CreateDirectories(const std::string& path);
bool ReadAll(const std::string& path, std::string& out);
// Writes through a temporary file and renames it over the target, so a crash
// never leaves a half-written config behind.
bool WriteAllAtomic(const std::string& path, const std::string& data);
bool CopyFileTo(const std::string& from, const std::string& to, bool overwrite);
bool MoveFileTo(const std::string& from, const std::string& to);
bool DeleteFileAt(const std::string& path);
std::vector<DirEntry> List(const std::string& dir);
// Recursively lists files below dir whose names end with one of the
// extensions (case-insensitive, include the dot). Empty list = all files.
std::vector<DirEntry> ListRecursive(const std::string& dir, const std::vector<std::string>& extensions,
                                    size_t maxEntries = 5000);

std::string Join(const std::string& a, const std::string& b);
std::string Parent(const std::string& path);
std::string FileName(const std::string& path);
std::string StripExtension(const std::string& name);
std::string Extension(const std::string& name);  // includes the dot, lower-case
// True when 'path' is inside 'root' after normalisation (rejects "..").
bool IsInside(const std::string& root, const std::string& path);
std::string Normalize(const std::string& path);

}  // namespace omm::fs
