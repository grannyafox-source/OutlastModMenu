#include "plugins.h"

#include "../../sdk/omm_plugin_api.h"
#include "outlast.h"
#include "state.h"

#include "../core/fileutil.h"
#include "../core/guard.h"
#include "../core/log.h"
#include "../core/paths.h"
#include "../core/strutil.h"
#include "../core/sync.h"
#include "../ue3/call.h"

#include "imgui.h"

namespace omm::plugins {

using namespace ue3;

namespace {
struct Callback {
    OMM_Callback fn;
    void* user;
};
struct Section {
    std::string title;
    OMM_Callback draw;
    void* user;
};

Mutex g_lock;
std::vector<Callback> g_frameCallbacks;
std::vector<Section> g_sections;
std::vector<PluginInfo> g_plugins;
std::string g_modDir;
std::string g_nameBuffer;  // ObjectName() result (game thread only)

bool GameThread(const char* what) {
    if (OnGameThread()) return true;
    LOGE("Plugin called %s off the game thread - ignored", what);
    return false;
}

UObject* AsObj(void* p) {
    auto* o = static_cast<UObject*>(p);
    return o && IsValid(o) ? o : nullptr;
}

// --- API implementation ------------------------------------------------------------
void ApiLog(const char* s) { LOGI("[plugin] %s", s ? s : ""); }
void ApiNotify(const char* s, float seconds) { game::Notify(s ? s : "", seconds > 0 ? seconds : 2.5f); }
const char* ApiModDir() { return g_modDir.c_str(); }
const char* ApiModVersion() { return OMM_VERSION_STRING; }

void ApiRegisterFrame(OMM_Callback fn, void* user) {
    if (!fn) return;
    LockGuard lock(g_lock);
    g_frameCallbacks.push_back({fn, user});
}
void ApiRegisterSection(const char* title, OMM_Callback draw, void* user) {
    if (!draw) return;
    LockGuard lock(g_lock);
    g_sections.push_back({title ? title : "Plugin", draw, user});
}
void* ApiImGuiContext() { return ImGui::GetCurrentContext(); }
void ApiImGuiAllocators(void** allocFunc, void** freeFunc, void** userData) {
    ImGuiMemAllocFunc a = nullptr;
    ImGuiMemFreeFunc f = nullptr;
    void* u = nullptr;
    ImGui::GetAllocatorFunctions(&a, &f, &u);
    if (allocFunc) *allocFunc = reinterpret_cast<void*>(a);
    if (freeFunc) *freeFunc = reinterpret_cast<void*>(f);
    if (userData) *userData = u;
}
const char* ApiImGuiVersion() { return IMGUI_VERSION; }
void ApiQueue(OMM_Callback fn, void* user) {
    if (fn) game::Enqueue([fn, user] { fn(user); });
}

void* ApiPC() { return GameThread("PlayerController") ? game::W().pc : nullptr; }
void* ApiHero() { return GameThread("Hero") ? game::W().hero : nullptr; }
void* ApiWorldInfo() { return GameThread("WorldInfo") ? game::W().worldInfo : nullptr; }
void* ApiFindObject(const char* name, const char* cls) {
    return GameThread("FindObject") && name ? FindObject(name, cls) : nullptr;
}
void* ApiFindClass(const char* name) { return GameThread("FindClass") && name ? FindClass(name) : nullptr; }
int ApiIsA(void* o, const char* cls) { return GameThread("IsA") && cls && AsObj(o) && IsA(AsObj(o), cls) ? 1 : 0; }
const char* ApiObjectName(void* o) {
    if (!GameThread("ObjectName") || !AsObj(o)) return "";
    g_nameBuffer = Name(AsObj(o));
    return g_nameBuffer.c_str();
}
int ApiGetFloat(void* o, const char* p, float* out) {
    return GameThread("GetFloat") && AsObj(o) && p && out && GetFloat(AsObj(o), p, *out) ? 1 : 0;
}
int ApiSetFloat(void* o, const char* p, float v) {
    return GameThread("SetFloat") && AsObj(o) && p && SetFloat(AsObj(o), p, v) ? 1 : 0;
}
int ApiGetInt(void* o, const char* p, int32_t* out) {
    return GameThread("GetInt") && AsObj(o) && p && out && GetInt(AsObj(o), p, *out) ? 1 : 0;
}
int ApiSetInt(void* o, const char* p, int32_t v) {
    return GameThread("SetInt") && AsObj(o) && p && SetInt(AsObj(o), p, v) ? 1 : 0;
}
int ApiGetBool(void* o, const char* p, int* out) {
    bool b = false;
    if (!GameThread("GetBool") || !AsObj(o) || !p || !out || !GetBool(AsObj(o), p, b)) return 0;
    *out = b ? 1 : 0;
    return 1;
}
int ApiSetBool(void* o, const char* p, int v) {
    return GameThread("SetBool") && AsObj(o) && p && SetBool(AsObj(o), p, v != 0) ? 1 : 0;
}
void* ApiGetObject(void* o, const char* p) { return GameThread("GetObject") && AsObj(o) && p ? Obj(AsObj(o), p) : nullptr; }
int ApiSetObject(void* o, const char* p, void* v) {
    return GameThread("SetObject") && AsObj(o) && p && SetObj(AsObj(o), p, static_cast<UObject*>(v)) ? 1 : 0;
}
int ApiCallNoArgs(void* o, const char* fn) {
    return GameThread("CallNoArgs") && AsObj(o) && fn && CallNoArgs(AsObj(o), fn) ? 1 : 0;
}
int ApiConsole(const char* cmd) {
    if (!GameThread("ConsoleCommand") || !cmd || !game::W().pc) return 0;
    game::Console(cmd);
    return 1;
}

OMM_Api MakeApi() {
    OMM_Api a{};
    a.version = OMM_API_VERSION;
    a.size = sizeof(OMM_Api);
    a.Log = ApiLog;
    a.Notify = ApiNotify;
    a.ModDirectory = ApiModDir;
    a.ModVersion = ApiModVersion;
    a.RegisterFrameCallback = ApiRegisterFrame;
    a.RegisterMenuSection = ApiRegisterSection;
    a.GetImGuiContext = ApiImGuiContext;
    a.ImGuiVersion = ApiImGuiVersion;
    a.GetImGuiAllocatorFunctions = ApiImGuiAllocators;
    a.QueueOnGameThread = ApiQueue;
    a.PlayerController = ApiPC;
    a.Hero = ApiHero;
    a.WorldInfo = ApiWorldInfo;
    a.FindObject = ApiFindObject;
    a.FindClass = ApiFindClass;
    a.IsA = ApiIsA;
    a.ObjectName = ApiObjectName;
    a.GetFloat = ApiGetFloat;
    a.SetFloat = ApiSetFloat;
    a.GetInt = ApiGetInt;
    a.SetInt = ApiSetInt;
    a.GetBool = ApiGetBool;
    a.SetBool = ApiSetBool;
    a.GetObjectProperty = ApiGetObject;
    a.SetObjectProperty = ApiSetObject;
    a.CallNoArgs = ApiCallNoArgs;
    a.ConsoleCommand = ApiConsole;
    return a;
}

const OMM_Api& Api() {
    static const OMM_Api api = MakeApi();
    return api;
}
}  // namespace

void LoadAll() {
    g_modDir = paths::ModDir();
    std::string dir = paths::ModSubdir("plugins");
#if OMM_WINDOWS
    for (const fs::DirEntry& e : fs::List(dir)) {
        std::string ext = fs::Extension(e.name);
        if (e.isDir || (ext != ".dll" && ext != ".asi")) continue;
        PluginInfo info;
        info.file = e.name;
        info.name = fs::StripExtension(e.name);
        HMODULE m = LoadLibraryW(str::Utf8ToWide(e.path).c_str());
        if (!m) {
            info.status = str::Format("could not be loaded (error %lu - wrong 32/64-bit build?)", GetLastError());
        } else {
            auto init = reinterpret_cast<OMM_PluginInitFn>(reinterpret_cast<void*>(GetProcAddress(m, "OMM_PluginInit")));
            auto name = reinterpret_cast<OMM_PluginNameFn>(reinterpret_cast<void*>(GetProcAddress(m, "OMM_PluginName")));
            if (name) {
                const char* n = name();
                if (n && *n) info.name = n;
            }
            if (!init) {
                info.loaded = true;
                info.status = "loaded (plain ASI, no OMM_PluginInit)";
            } else {
                int rc = init(&Api());
                info.loaded = rc == 0;
                info.status = rc == 0 ? "running" : str::Format("OMM_PluginInit failed (%d)", rc);
            }
        }
        LOGI("Plugin %s: %s", e.name.c_str(), info.status.c_str());
        LockGuard lock(g_lock);
        g_plugins.push_back(info);
    }
#endif
}

void OnGameFrame() {
    std::vector<Callback> copy;
    {
        LockGuard lock(g_lock);
        if (g_frameCallbacks.empty()) return;
        copy = g_frameCallbacks;
    }
    for (const Callback& c : copy) {
        if (guard::Run("plugin frame callback", [&] { c.fn(c.user); })) continue;
        // A plugin that crashes is not called again.
        LockGuard lock(g_lock);
        for (auto it = g_frameCallbacks.begin(); it != g_frameCallbacks.end(); ++it)
            if (it->fn == c.fn && it->user == c.user) {
                g_frameCallbacks.erase(it);
                break;
            }
        game::Notify("A plugin crashed and was disabled - see the log", 6.f);
    }
}

void DrawMenuSections() {
    std::vector<Section> copy;
    {
        LockGuard lock(g_lock);
        copy = g_sections;
    }
    for (const Section& s : copy) {
        ImGui::PushID(s.title.c_str());
        ImGui::SeparatorText(s.title.c_str());
        s.draw(s.user);
        ImGui::PopID();
    }
}

bool HasMenuSections() {
    LockGuard lock(g_lock);
    return !g_sections.empty();
}

std::vector<PluginInfo> List() {
    LockGuard lock(g_lock);
    return g_plugins;
}

}  // namespace omm::plugins

extern "C" const OMM_Api* OMM_GetApi(void) { return &omm::plugins::Api(); }
