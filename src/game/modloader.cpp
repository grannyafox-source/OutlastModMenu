#include "modloader.h"

#include "outlast.h"
#include "state.h"

#include "../core/fileutil.h"
#include "../core/ini.h"
#include "../core/log.h"
#include "../core/paths.h"
#include "../core/strutil.h"

#include <algorithm>
#include <cctype>

#if OMM_WINDOWS
#include <shellapi.h>
#endif

namespace omm::game::mods {

namespace {
constexpr const char* kModsInstallDir = "OMM_Mods";

std::string ModsDir() { return paths::ModSubdir("mods"); }
std::string BackupDir(const std::string& folder) { return fs::Join(paths::ModSubdir("mod_backup"), folder); }
std::string ManifestPath(const std::string& folder) { return fs::Join(BackupDir(folder), "installed.txt"); }

// Path of `file` relative to `root` (both absolute, file inside root).
std::string Relative(const std::string& root, const std::string& file) {
    std::string r = fs::Normalize(root), f = fs::Normalize(file);
    if (f.size() <= r.size()) return std::string();
    std::string rel = f.substr(r.size());
    while (!rel.empty() && (rel[0] == '\\' || rel[0] == '/')) rel.erase(0, 1);
    return rel;
}

struct PlannedCopy {
    std::string from;
    std::string to;
};

std::vector<PlannedCopy> Plan(const ContentMod& m, std::string& error) {
    std::vector<PlannedCopy> plan;
    std::string src = fs::Join(ModsDir(), m.folder);
    std::string cooked = fs::Join(src, "CookedPCConsole");
    std::string cookedDest = fs::Join(paths::CookedDir(), fs::Join(kModsInstallDir, m.folder));
    for (const fs::DirEntry& e : fs::ListRecursive(cooked, {}, 5000)) {
        std::string rel = Relative(cooked, e.path);
        if (!rel.empty()) plan.push_back({e.path, fs::Join(cookedDest, rel)});
    }
    std::string game = fs::Join(src, "Game");
    const std::string& root = paths::GameRoot();
    for (const fs::DirEntry& e : fs::ListRecursive(game, {}, 5000)) {
        std::string rel = Relative(game, e.path);
        std::string to = fs::Join(root, rel);
        if (rel.empty() || !fs::IsInside(root, to)) {
            error = "refusing to install outside the game folder: " + rel;
            return {};
        }
        plan.push_back({e.path, to});
    }
    return plan;
}
}  // namespace

std::vector<ContentMod> ScanMods() {
    std::vector<ContentMod> out;
    for (const fs::DirEntry& e : fs::List(ModsDir())) {
        if (!e.isDir) continue;
        ContentMod m;
        m.folder = e.name;
        m.name = e.name;
        IniDocument doc;
        if (doc.Load(fs::Join(e.path, "mod.ini"))) {
            m.name = doc.Get("Mod", "Name").value_or(e.name);
            m.author = doc.Get("Mod", "Author").value_or("");
            m.version = doc.Get("Mod", "Version").value_or("");
            m.description = doc.Get("Mod", "Description").value_or("");
            m.startMap = doc.Get("Mod", "StartMap").value_or("");
            m.startCheckpoint = doc.Get("Mod", "StartCheckpoint").value_or("");
        }
        std::string err;
        m.fileCount = static_cast<int>(Plan(m, err).size());
        m.installed = fs::Exists(ManifestPath(m.folder));
        out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](const ContentMod& a, const ContentMod& b) { return a.name < b.name; });
    return out;
}

bool Install(const ContentMod& m, std::string& error) {
    if (m.folder.empty() || m.folder.find("..") != std::string::npos) {
        error = "bad mod folder";
        return false;
    }
    std::vector<PlannedCopy> plan = Plan(m, error);
    if (!error.empty()) return false;
    if (plan.empty()) {
        error = "the mod has no CookedPCConsole\\ or Game\\ folder with files";
        return false;
    }
    std::string backup = BackupDir(m.folder);
    fs::CreateDirectories(backup);
    std::string manifest;
    int copied = 0;
    for (const PlannedCopy& c : plan) {
        fs::CreateDirectories(fs::Parent(c.to));
        if (fs::Exists(c.to)) {
            // Keep the original game file so uninstall can put it back.
            std::string rel = Relative(paths::GameRoot(), c.to);
            std::string saved = fs::Join(fs::Join(backup, "files"), rel);
            fs::CreateDirectories(fs::Parent(saved));
            if (!fs::Exists(saved) && !fs::CopyFileTo(c.to, saved, false)) {
                error = "could not back up " + c.to;
                return false;
            }
            manifest += "B|" + c.to + "|" + saved + "\n";
        } else {
            manifest += "N|" + c.to + "\n";
        }
        if (!fs::CopyFileTo(c.from, c.to, true)) {
            error = "could not copy " + c.from + " (is the game folder writable?)";
            fs::WriteAllAtomic(ManifestPath(m.folder), manifest);
            return false;
        }
        ++copied;
    }
    if (!fs::WriteAllAtomic(ManifestPath(m.folder), manifest)) {
        error = "could not write the install manifest";
        return false;
    }
    LOGI("Installed mod '%s' (%d files)", m.name.c_str(), copied);
    return true;
}

bool Uninstall(const ContentMod& m, std::string& error) {
    std::string text;
    if (!fs::ReadAll(ManifestPath(m.folder), text)) {
        error = "the mod is not installed";
        return false;
    }
    int restored = 0, removed = 0, failed = 0;
    for (const std::string& line : str::Split(text, '\n')) {
        std::vector<std::string> p = str::Split(line, '|', false);
        if (p.size() >= 2 && p[0] == "N") {
            if (!fs::IsInside(paths::GameRoot(), p[1])) continue;
            if (fs::DeleteFileAt(p[1]) || !fs::Exists(p[1])) ++removed;
            else ++failed;
        } else if (p.size() >= 3 && p[0] == "B") {
            if (!fs::IsInside(paths::GameRoot(), p[1])) continue;
            if (fs::CopyFileTo(p[2], p[1], true)) ++restored;
            else ++failed;
        }
    }
    if (failed) {
        error = str::Format("%d file(s) could not be restored or removed (is the game using them?)", failed);
        return false;
    }
    fs::DeleteFileAt(ManifestPath(m.folder));
    LOGI("Uninstalled mod '%s' (%d removed, %d restored)", m.name.c_str(), removed, restored);
    return true;
}

std::vector<MapFile> ListMaps() {
    std::vector<MapFile> out;
    const std::string& cooked = paths::CookedDir();
    for (const fs::DirEntry& e : fs::ListRecursive(cooked, {".udk", ".umap"}, 4000)) {
        MapFile m;
        m.name = fs::StripExtension(e.name);
        m.relPath = Relative(cooked, e.path);
        m.fromMod = str::IStartsWith(m.relPath, kModsInstallDir);
        out.push_back(m);
    }
    // Packages installed by content mods can be maps saved as .upk.
    std::string modsRoot = fs::Join(cooked, kModsInstallDir);
    for (const fs::DirEntry& e : fs::ListRecursive(modsRoot, {".upk"}, 2000)) {
        MapFile m;
        m.name = fs::StripExtension(e.name);
        m.relPath = Relative(cooked, e.path);
        m.fromMod = true;
        out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](const MapFile& a, const MapFile& b) {
        if (a.fromMod != b.fromMod) return a.fromMod;
        return a.name < b.name;
    });
    return out;
}

bool OpenMap(const std::string& map) {
    std::string clean;
    for (char c : map)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') clean.push_back(c);
    if (clean.empty() || !W().pc) return false;
    LOGI("Opening map %s", clean.c_str());
    Console("open " + clean);
    return true;
}

// --- ReShade ---------------------------------------------------------------------

ReShadeInfo DetectReShade() {
    ReShadeInfo info;
    const std::string& bin = paths::BinariesDir();
    for (const char* ini : {"ReShade.ini", "dxgi.ini", "d3d9.ini", "d3d11.ini"}) {
        std::string p = fs::Join(bin, ini);
        if (fs::Exists(p)) {
            info.configPath = p;
            break;
        }
    }
    for (const char* dll : {"dxgi.dll", "d3d9.dll", "d3d11.dll", "opengl32.dll"}) {
        if (fs::Exists(fs::Join(bin, dll))) {
            info.dll = dll;
            break;
        }
    }
    info.installed = !info.configPath.empty() && !info.dll.empty();
    if (!info.configPath.empty()) {
        IniDocument doc;
        if (doc.Load(info.configPath)) {
            info.activePreset = doc.Get("GENERAL", "PresetPath").value_or("");
            if (info.activePreset.empty()) info.activePreset = doc.Get("GENERAL", "CurrentPresetPath").value_or("");
        }
    }
    // Presets: .ini files with a Techniques= line next to the game or in
    // the usual preset folders.
    std::vector<std::string> dirs = {bin, fs::Join(bin, "reshade-presets"), fs::Join(bin, "ReShade Presets")};
    for (const std::string& d : dirs) {
        for (const fs::DirEntry& e : fs::List(d)) {
            if (e.isDir || fs::Extension(e.name) != ".ini" || e.size > 512 * 1024) continue;
            std::string text;
            if (!fs::ReadAll(e.path, text) || text.find("Techniques=") == std::string::npos) continue;
            info.presets.push_back({fs::StripExtension(e.name), e.path});
        }
    }
    return info;
}

bool SetReShadePreset(const ReShadeInfo& info, const std::string& presetPath, std::string& error) {
    if (info.configPath.empty()) {
        error = "ReShade.ini not found next to OLGame.exe";
        return false;
    }
    IniDocument doc;
    if (!doc.Load(info.configPath)) {
        error = "could not read " + info.configPath;
        return false;
    }
    // ReShade accepts paths relative to the game executable.
    std::string value = presetPath;
    std::string rel = Relative(paths::BinariesDir(), presetPath);
    if (!rel.empty()) value = ".\\" + rel;
    doc.Set("GENERAL", "PresetPath", value);
    if (!doc.Save(info.configPath)) {
        error = "could not write " + info.configPath;
        return false;
    }
    return true;
}

int InstallBundledReShadePresets(std::string& error) {
    std::string src = paths::ModSubdir("reshade");
    std::string dest = fs::Join(paths::BinariesDir(), "reshade-presets");
    fs::CreateDirectories(dest);
    int n = 0;
    for (const fs::DirEntry& e : fs::List(src)) {
        if (e.isDir || fs::Extension(e.name) != ".ini") continue;
        if (fs::CopyFileTo(e.path, fs::Join(dest, e.name), true)) ++n;
        else error = "could not copy " + e.name;
    }
    return n;
}

void OpenFolder(const std::string& path) {
#if OMM_WINDOWS
    fs::CreateDirectories(path);
    ShellExecuteW(nullptr, L"open", str::Utf8ToWide(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)path;
#endif
}

}  // namespace omm::game::mods
