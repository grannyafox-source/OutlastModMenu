#include "paths.h"

#include "common.h"
#include "fileutil.h"
#include "log.h"
#include "strutil.h"

#if OMM_WINDOWS
#include <shlobj.h>
#endif

namespace omm::paths {

namespace {
std::string g_dll, g_modDir, g_exe, g_bin, g_root, g_cfg, g_cooked;

#if OMM_WINDOWS
std::string ModulePath(HMODULE m) {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameW(m, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    return str::WideToUtf8(std::wstring(buf, n));
}

std::string DocumentsDir() {
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, buf)))
        return str::WideToUtf8(buf);
    return std::string();
}
#endif
}  // namespace

void Init(void* modDllModule) {
    (void)modDllModule;
#if OMM_WINDOWS
    g_dll = ModulePath(static_cast<HMODULE>(modDllModule));
    g_exe = ModulePath(nullptr);
#endif
    g_bin = fs::Parent(g_exe);
    // Binaries\Win64\OLGame.exe -> install root is two levels up.
    g_root = fs::Parent(fs::Parent(g_bin));
    if (!fs::IsDirectory(fs::Join(g_root, "OLGame"))) {
        // Unusual layout (e.g. exe copied next to the content). Walk upwards.
        std::string cur = g_bin;
        for (int i = 0; i < 4 && !cur.empty(); ++i) {
            if (fs::IsDirectory(fs::Join(cur, "OLGame"))) {
                g_root = cur;
                break;
            }
            cur = fs::Parent(cur);
        }
    }

    g_modDir = fs::Join(fs::Parent(g_dll.empty() ? g_exe : g_dll), "OutlastModMenu");
    fs::CreateDirectories(g_modDir);

    g_cooked = fs::Join(fs::Join(g_root, "OLGame"), "CookedPCConsole");
    if (!fs::IsDirectory(g_cooked)) g_cooked = fs::Join(fs::Join(g_root, "OLGame"), "CookedPC");

    // UE3 keeps user INIs in Documents\My Games\<MyDocumentsSubDirName> unless
    // the game was started with -nohomedir.
    std::string rootCfg = fs::Join(fs::Join(g_root, "OLGame"), "Config");
    std::string homeCfg;
#if OMM_WINDOWS
    std::string docs = DocumentsDir();
    if (!docs.empty()) homeCfg = fs::Join(fs::Join(fs::Join(fs::Join(docs, "My Games"), "Outlast"), "OLGame"), "Config");
    std::string cmd = str::ToLower(str::WideToUtf8(GetCommandLineW()));
    bool noHomeDir = cmd.find("-nohomedir") != std::string::npos;
#else
    bool noHomeDir = false;
#endif
    if (!noHomeDir && !homeCfg.empty() && fs::Exists(fs::Join(homeCfg, "OLEngine.ini")))
        g_cfg = homeCfg;
    else if (fs::Exists(fs::Join(rootCfg, "OLEngine.ini")))
        g_cfg = rootCfg;
    else
        g_cfg = !homeCfg.empty() ? homeCfg : rootCfg;
}

void SetForTests(const std::string& modDir, const std::string& gameRoot, const std::string& configDir) {
    g_modDir = modDir;
    g_root = gameRoot;
    g_cfg = configDir;
    g_bin = fs::Join(fs::Join(gameRoot, "Binaries"), "Win64");
    g_exe = fs::Join(g_bin, "OLGame.exe");
    g_cooked = fs::Join(fs::Join(gameRoot, "OLGame"), "CookedPCConsole");
    fs::CreateDirectories(g_modDir);
}

const std::string& ModDllPath() { return g_dll; }
const std::string& ModDir() { return g_modDir; }
const std::string& GameExePath() { return g_exe; }
const std::string& BinariesDir() { return g_bin; }
const std::string& GameRoot() { return g_root; }
const std::string& UserConfigDir() { return g_cfg; }
const std::string& CookedDir() { return g_cooked; }

std::string ModSubdir(const char* name) {
    std::string p = fs::Join(g_modDir, name);
    fs::CreateDirectories(p);
    return p;
}

std::string ConfigFile(const char* fileName) { return fs::Join(g_cfg, fileName); }

}  // namespace omm::paths
