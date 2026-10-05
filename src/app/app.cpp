#include "app.h"

#if OMM_WINDOWS

#include "../core/fileutil.h"
#include "../core/guard.h"
#include "../core/log.h"
#include "../core/paths.h"
#include "../core/settings.h"
#include "../core/strutil.h"
#include "../game/enemies.h"
#include "../game/frame.h"
#include "../game/initweaks.h"
#include "../game/outlast.h"
#include "../game/plugins.h"
#include "../game/state.h"
#include "../render/input.h"
#include "../render/overlay.h"
#include "../ue3/bootstrap.h"
#include "../ue3/call.h"
#include "../ue3/engine.h"
#include "../ue3/hooks.h"
#include "../ui/ui.h"

#include <cstdlib>

namespace omm::proxy {
HMODULE RealDInput8();
}

namespace omm::app {

namespace {
HANDLE g_instanceMutex = nullptr;
volatile uint64_t g_lastPrimaryFrameMs = 0;

void PublishStatus(const std::string& status) {
    game::Snapshot snap;
    snap.engineReady = false;
    snap.engineStatus = status;
    game::state::PublishSnapshot(std::move(snap));
}

// Primary per-frame hook: GameViewportClient.PostRender. Each part of the
// frame has its own crash guard; this outer one catches anything else.
void OnViewportPostRender(ue3::UObject* self) {
    g_lastPrimaryFrameMs = NowMs();
    guard::Run("frame", [&] { game::OnGameFrame(self); });
}

// Fallback: HUD.PostRender, used only while the primary hook is silent.
void OnHudPostRender(ue3::UObject*) {
    if (NowMs() - g_lastPrimaryFrameMs < 500) return;
    guard::Run("frame", [] { game::OnGameFrame(nullptr); });
}

ue3::UObject* FindViewportClient() {
    ue3::UObject* engine = game::FindEngine();
    ue3::UObject* vp = ue3::Obj(engine, "GameViewport");
    return vp && ue3::IsValid(vp) ? vp : nullptr;
}

bool HookFrame(std::string& how) {
    ue3::UObject* vp = FindViewportClient();
    if (!vp) return false;
    ue3::UFunction* fn = ue3::FindFunction(ue3::ClassOf(vp), "PostRender");
    if (!fn) {
        how = "GameViewportClient.PostRender not found";
        return false;
    }
    std::string name = ue3::FullName(fn);  // before the hook goes live (see HookFunction)
    if (!ue3::HookFunction(fn, &OnViewportPostRender)) {
        how = "could not hook " + name;
        return false;
    }
    how = name;
    return true;
}

void InstallFallbackHooks() {
    // Resolve everything first: once a hook is live the game thread owns the
    // reflection caches.
    ue3::UObject* vp = FindViewportClient();
    ue3::UObject* engine = game::FindEngine();
    std::vector<ue3::UObject*> players;
    ue3::UObject* hud = nullptr;
    if (engine && ue3::GetObjArray(engine, "GamePlayers", players) && !players.empty()) {
        ue3::UObject* pc = ue3::Obj(players[0], "Actor");
        hud = ue3::Obj(pc, "myHUD");  // exists in the main menu too
    }
    ue3::UFunction* hudFn = hud ? ue3::FindFunction(ue3::ClassOf(hud), "PostRender") : nullptr;
    ue3::UFunction* vpFn = vp ? ue3::FindFunction(ue3::ClassOf(vp), "PostRender") : nullptr;
    if (hudFn && ue3::HookFunction(hudFn, &OnHudPostRender)) LOGW("Fallback frame hook on HUD.PostRender installed");
    Sleep(3000);
    if (vpFn && ue3::HookCalls() == 0) ue3::InstallProcessInternalHook(vpFn, &OnViewportPostRender);
}

std::string ExeName() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    const wchar_t* name = wcsrchr(buf, L'\\');
    return str::ToLower(str::WideToUtf8(name ? name + 1 : buf));
}
}  // namespace

bool ShouldStart(HMODULE self) {
    (void)self;
    std::string exe = ExeName();
    if (!str::IStartsWith(exe, "olgame") && !str::IStartsWith(exe, "outlast")) return false;
    // Kernel32 only (this runs under the loader lock): build the name by hand.
    std::wstring name = L"Local\\OutlastModMenu_";
    const size_t prefix = name.size();
    for (DWORD pid = GetCurrentProcessId(); pid; pid /= 10)
        name.insert(name.begin() + static_cast<std::ptrdiff_t>(prefix), static_cast<wchar_t>(L'0' + pid % 10));
    g_instanceMutex = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!g_instanceMutex) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_instanceMutex);
        g_instanceMutex = nullptr;
        return false;  // another copy (e.g. dinput8.dll and an .asi) is already running
    }
    return true;
}

DWORD WINAPI InitThread(LPVOID selfModule) {
    paths::Init(selfModule);
    log::Init(fs::Join(paths::ModDir(), "OutlastModMenu.log"));
    guard::Install();
    LOGI("%s %s (%s) loaded into %s", OMM_NAME, OMM_VERSION_STRING, OMM_X64 ? "64-bit" : "32-bit",
         paths::GameExePath().c_str());
    LOGI("Mod folder: %s", paths::ModDir().c_str());
    LOGI("Game folder: %s | config: %s", paths::GameRoot().c_str(), paths::UserConfigDir().c_str());
    std::srand(static_cast<unsigned>(GetTickCount()));

    Settings::Get().Load(fs::Join(paths::ModDir(), "config.ini"));
    game::state::Load();
    game::enemies::LoadModelRules();
    int fixed = game::ini::ReapplyActive();
    if (fixed) game::Notify("Some INI tweaks had been reset by the game and were re-applied (restart to use them).", 6.f);
    for (const char* sub : {"mods", "models", "plugins", "reshade"}) paths::ModSubdir(sub);

    PublishStatus("installing hooks");
    input::SetHotkeyHandler(&ui::OnHotkey);
    input::InstallDirectInputHooks(proxy::RealDInput8());
    render::SetCallbacks(&ui::Setup, &ui::Frame);
    if (!render::InstallHooks()) LOGE("No graphics API could be hooked - the menu will not be visible");

    // Wait for the engine's object tables (they appear a moment after start).
    // The engine is still loading while we look, so every pass is guarded.
    std::string status;
    uint64_t start = NowMs();
    for (;;) {
        bool found = false;
        guard::Run("engine scan", [&] { found = ue3::Bootstrap(status); });
        if (found) break;
        PublishStatus("looking for the engine: " + status);
        if (NowMs() - start > 10 * 60 * 1000) {
            LOGE("Engine not found after 10 minutes - giving up. Status: %s", status.c_str());
            PublishStatus("engine not found (see the log)");
            return 0;
        }
        Sleep(250);
    }
    LOGI("Engine found after %llu ms", static_cast<unsigned long long>(NowMs() - start));

    // Hook the per-frame event once the viewport exists.
    PublishStatus("waiting for the game viewport");
    std::string how, lastError;
    uint64_t lastRescan = NowMs();
    for (;;) {
        bool hooked = false;
        how.clear();
        guard::Run("frame hook setup", [&] { hooked = HookFrame(how); });
        if (hooked) break;
        if (ue3::L().funcFunc < 0) {
            // Functions can't be hooked without the UFunction layout; keep
            // looking for it (script classes may still be linking).
            how = "the engine's function layout is not known yet";
            uint64_t every = NowMs() - start < 5 * 60 * 1000 ? 5000 : 30000;
            if (NowMs() - lastRescan > every) {
                lastRescan = NowMs();
                std::string st;
                guard::Run("engine rescan", [&] { ue3::Bootstrap(st); });
            }
        }
        if (!how.empty() && how != lastError) {
            LOGE("Frame hook not installed yet: %s", how.c_str());
            PublishStatus("frame hook: " + how);
            lastError = how;
        }
        Sleep(500);
    }
    LOGI("Frame hook installed on %s", how.c_str());
    PublishStatus("waiting for the first frame");

    plugins::LoadAll();

    // Watchdog: if the primary hook never fires, try the fallbacks.
    uint64_t hookedAt = NowMs();
    bool fallbackTried = false;
    for (;;) {
        Sleep(1000);
        Settings::Get().SaveIfDirty(NowMs());
        if (!fallbackTried && ue3::HookCalls() == 0 && NowMs() - hookedAt > 8000) {
            fallbackTried = true;
            LOGW("The frame hook has not fired yet - installing fallback hooks");
            // Nothing on the game thread runs mod code yet, so this thread may
            // still use the reflection layer.
            guard::Run("fallback hooks", [] { InstallFallbackHooks(); });
        }
    }
}

}  // namespace omm::app

#endif  // OMM_WINDOWS
