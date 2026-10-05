#include "fileutil.h"

#include "common.h"
#include "strutil.h"

#include <cstdio>

#if !OMM_WINDOWS
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace omm::fs {

namespace {
#if OMM_WINDOWS
std::wstring W(const std::string& s) { return str::Utf8ToWide(s); }
#endif
const char kSep =
#if OMM_WINDOWS
    '\\';
#else
    '/';
#endif

bool IsSep(char c) { return c == '\\' || c == '/'; }
}  // namespace

bool Exists(const std::string& path) {
#if OMM_WINDOWS
    return GetFileAttributesW(W(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

bool IsDirectory(const std::string& path) {
#if OMM_WINDOWS
    DWORD a = GetFileAttributesW(W(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool CreateDirectories(const std::string& path) {
    if (path.empty()) return false;
    if (IsDirectory(path)) return true;
    std::string parent = Parent(path);
    if (!parent.empty() && parent != path && !IsDirectory(parent)) {
        if (!CreateDirectories(parent)) return false;
    }
#if OMM_WINDOWS
    return CreateDirectoryW(W(path).c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path.c_str(), 0755) == 0 || IsDirectory(path);
#endif
}

bool ReadAll(const std::string& path, std::string& out) {
    out.clear();
#if OMM_WINDOWS
    FILE* f = _wfopen(W(path).c_str(), L"rb");
#else
    FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

bool WriteAllAtomic(const std::string& path, const std::string& data) {
    std::string tmp = path + ".omm_tmp";
#if OMM_WINDOWS
    FILE* f = _wfopen(W(tmp).c_str(), L"wb");
#else
    FILE* f = std::fopen(tmp.c_str(), "wb");
#endif
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) {
        DeleteFileAt(tmp);
        return false;
    }
#if OMM_WINDOWS
    if (!MoveFileExW(W(tmp).c_str(), W(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileAt(tmp);
        return false;
    }
    return true;
#else
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        DeleteFileAt(tmp);
        return false;
    }
    return true;
#endif
}

bool CopyFileTo(const std::string& from, const std::string& to, bool overwrite) {
#if OMM_WINDOWS
    return CopyFileW(W(from).c_str(), W(to).c_str(), overwrite ? FALSE : TRUE) != 0;
#else
    if (!overwrite && Exists(to)) return false;
    std::string data;
    if (!ReadAll(from, data)) return false;
    return WriteAllAtomic(to, data);
#endif
}

bool MoveFileTo(const std::string& from, const std::string& to) {
#if OMM_WINDOWS
    return MoveFileExW(W(from).c_str(), W(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
#else
    return std::rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool DeleteFileAt(const std::string& path) {
#if OMM_WINDOWS
    return DeleteFileW(W(path).c_str()) != 0;
#else
    return std::remove(path.c_str()) == 0;
#endif
}

std::vector<DirEntry> List(const std::string& dir) {
    std::vector<DirEntry> out;
#if OMM_WINDOWS
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(W(Join(dir, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string name = str::WideToUtf8(fd.cFileName);
        if (name == "." || name == "..") continue;
        DirEntry e;
        e.name = name;
        e.path = Join(dir, name);
        e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        out.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* ent = readdir(d)) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        DirEntry e;
        e.name = name;
        e.path = Join(dir, name);
        struct stat st;
        if (stat(e.path.c_str(), &st) == 0) {
            e.isDir = S_ISDIR(st.st_mode);
            e.size = static_cast<uint64_t>(st.st_size);
        }
        out.push_back(e);
    }
    closedir(d);
#endif
    return out;
}

std::vector<DirEntry> ListRecursive(const std::string& dir, const std::vector<std::string>& extensions,
                                    size_t maxEntries) {
    std::vector<DirEntry> out;
    std::vector<std::string> stack{dir};
    while (!stack.empty() && out.size() < maxEntries) {
        std::string cur = stack.back();
        stack.pop_back();
        for (const DirEntry& e : List(cur)) {
            if (e.isDir) {
                stack.push_back(e.path);
                continue;
            }
            bool match = extensions.empty();
            for (const std::string& ext : extensions)
                if (str::IEndsWith(e.name, ext)) { match = true; break; }
            if (match) out.push_back(e);
            if (out.size() >= maxEntries) break;
        }
    }
    return out;
}

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (IsSep(a.back())) return a + b;
    return a + kSep + b;
}

std::string Parent(const std::string& path) {
    size_t end = path.size();
    while (end > 0 && IsSep(path[end - 1])) --end;
    while (end > 0 && !IsSep(path[end - 1])) --end;
    while (end > 1 && IsSep(path[end - 1])) --end;
    return path.substr(0, end);
}

std::string FileName(const std::string& path) {
    size_t end = path.size();
    while (end > 0 && IsSep(path[end - 1])) --end;
    size_t b = end;
    while (b > 0 && !IsSep(path[b - 1])) --b;
    return path.substr(b, end - b);
}

std::string StripExtension(const std::string& name) {
    size_t dot = name.find_last_of('.');
    size_t sep = name.find_last_of("\\/");
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return name;
    return name.substr(0, dot);
}

std::string Extension(const std::string& name) {
    size_t dot = name.find_last_of('.');
    size_t sep = name.find_last_of("\\/");
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return std::string();
    return str::ToLower(name.substr(dot));
}

std::string Normalize(const std::string& path) {
    std::vector<std::string> parts;
    std::string prefix;
    size_t i = 0;
    // Keep a drive letter ("C:") or a leading separator.
    if (path.size() >= 2 && path[1] == ':') {
        prefix = path.substr(0, 2);
        i = 2;
    }
    if (i < path.size() && IsSep(path[i])) {
        prefix.push_back(kSep);
        ++i;
    }
    std::string cur;
    auto flush = [&]() {
        if (cur.empty() || cur == ".") {
        } else if (cur == "..") {
            if (!parts.empty() && parts.back() != "..") parts.pop_back();
            else parts.push_back("..");
        } else {
            parts.push_back(cur);
        }
        cur.clear();
    };
    for (; i < path.size(); ++i) {
        if (IsSep(path[i])) flush();
        else cur.push_back(path[i]);
    }
    flush();
    std::string out = prefix;
    for (size_t k = 0; k < parts.size(); ++k) {
        if (k) out.push_back(kSep);
        out += parts[k];
    }
    return out;
}

bool IsInside(const std::string& root, const std::string& path) {
    std::string r = str::ToLower(Normalize(root));
    std::string p = str::ToLower(Normalize(path));
    if (r.empty() || p.size() <= r.size()) return false;
    if (p.compare(0, r.size(), r) != 0) return false;
    if (!IsSep(r.back()) && !IsSep(p[r.size()])) return false;
    return p.find("..") == std::string::npos;
}

}  // namespace omm::fs
