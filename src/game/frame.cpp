#include "frame.h"

#include "actions.h"
#include "enemies.h"
#include "features.h"
#include "outlast.h"
#include "performance.h"
#include "plugins.h"
#include "state.h"

#include "../core/guard.h"
#include "../core/log.h"
#include "../core/settings.h"
#include "../core/strutil.h"
#include "../core/sync.h"
#include "../render/input.h"
#include "../ue3/call.h"

#include <chrono>

namespace omm::game {

using namespace ue3;

namespace {
uint64_t g_frame = 0;
uint64_t g_lastFrameMs = 0;
UObject* g_lastWorldInfo = nullptr;
UObject* g_lastPc = nullptr;
uint64_t g_lastPurge = 0;
float g_rouletteTimer = 0.f;

Mutex g_statsLock;
FrameStats g_stats;

// Each part of the frame runs under the crash guard. A part that faults is
// paused for a few seconds (objects may have been freed during a level
// change); one that faults three times within two minutes stays off for the
// session. Isolated faults, e.g. one per level change, are forgiven.
struct Phase {
    const char* name;
    int faults = 0;
    uint64_t lastFault = 0;
    uint64_t pausedUntil = 0;
    bool disabled = false;
};

template <typename F>
bool RunPhase(Phase& p, F&& f) {
    if (p.disabled || NowMs() < p.pausedUntil) return false;
    if (guard::Run(p.name, f)) return true;
    uint64_t now = NowMs();
    if (now - p.lastFault > 120000) p.faults = 0;
    p.lastFault = now;
    if (++p.faults >= 3) {
        p.disabled = true;
        LOGE("'%s' failed %d times in two minutes and was switched off for this session", p.name, p.faults);
        Notify(str::Format("Mod error in '%s' - switched off to protect the game (see the log)", p.name), 8.f);
    } else {
        p.pausedUntil = now + 5000;
        Notify(str::Format("Mod error in '%s' - retrying in a moment", p.name), 4.f);
    }
    return false;
}

Phase g_pWorld{"reading game objects"};
Phase g_pLevel{"level change"};
Phase g_pActions{"menu actions"};
Phase g_pPlayer{"player options"};
Phase g_pVisuals{"visual options"};
Phase g_pWorldOpts{"world options"};
Phase g_pEnemies{"enemy options"};
Phase g_pRoulette{"teleport roulette"};
Phase g_pPerf{"performance mode"};
Phase g_pPause{"menu pause"};
Phase g_pPlugins{"plugins"};
Phase g_pSnapPlayer{"player status"};
Phase g_pSnapMenu{"game menu detection"};
Phase g_pSnapEnemies{"enemy list"};
Phase g_pEsp{"ESP"};
Phase g_pPurge{"cleanup"};

// --- Pause while the mod menu is open ---------------------------------------------
UObject* g_pausedWith = nullptr;  // PlayerReplicationInfo we put into WorldInfo.Pauser
UObject* g_pausedWorld = nullptr;

void ApplyMenuPause(bool want) {
    UObject* wi = W().worldInfo;
    if (g_pausedWith && g_pausedWorld != wi) {
        g_pausedWith = nullptr;  // level changed while paused: nothing to undo
        g_pausedWorld = nullptr;
    }
    if (want && !g_pausedWith) {
        UObject* pri = Obj(W().pc, "PlayerReplicationInfo");
        if (wi && pri && !Obj(wi, "Pauser") && W().hero) {
            SetObj(wi, "Pauser", pri);
            g_pausedWith = pri;
            g_pausedWorld = wi;
        }
    } else if (!want && g_pausedWith) {
        // Only undo our own pause; if the game changed it meanwhile, leave it.
        if (wi && Obj(wi, "Pauser") == g_pausedWith) SetObj(wi, "Pauser", nullptr);
        g_pausedWith = nullptr;
        g_pausedWorld = nullptr;
    }
}

// --- Which of the game's own menus is showing ----------------------------------------
// OLHUD.MenuManager is an OLUIFrontEnd (Scaleform movie). Its ViewStack holds
// the open screens, e.g. OLUIFrontEnd_MainMenu / OLUIFrontEnd_Options.
void FillMenuState(Snapshot& snap) {
    UObject* mm = Obj(W().hud, "MenuManager");
    if (!mm || !Bool(mm, "bMovieIsOpen")) return;
    uint8_t type = 0;
    GetByte(mm, "MenuType", type);  // EMenuType: 0 main menu, 1 pause, 2 tab, 3 recordings, 4 evidence
    std::vector<UObject*> stack;
    GetObjArray(mm, "ViewStack", stack);
    UObject* top = stack.empty() ? nullptr : stack.back();
    snap.menuView = top && IsValid(top) ? Name(ClassOf(top)) : std::string();
    if (str::IContains(snap.menuView, "Options")) snap.menu = GameMenu::Options;
    else if (type == 0) snap.menu = GameMenu::MainMenu;
    else if (type == 1) snap.menu = GameMenu::Pause;
    else snap.menu = GameMenu::Other;
}

void OnWorldChanged() {
    LOGI("Level changed: map '%s', checkpoint '%s'", MapName().c_str(), CurrentCheckpoint().c_str());
    enemies::Refresh(true);
    actions::RefreshGameCheckpointList();
    perf::Reapply();
    g_rouletteTimer = 0.f;
}

void Roulette(const ModState& s, float dt) {
    if (s.rouletteMode == 0 || !W().hero) {
        g_rouletteTimer = 0.f;
        return;
    }
    g_rouletteTimer += dt;
    if (g_rouletteTimer < std::max(10.f, s.rouletteInterval)) return;
    g_rouletteTimer = 0.f;
    switch (s.rouletteMode) {
        case 1: actions::TeleportRandomLocation(); break;
        case 2: {
            std::string cp = actions::RandomCheckpoint(s.rouletteIncludeMain, s.rouletteIncludeDlc);
            if (!cp.empty()) actions::LoadCheckpoint(cp);
            break;
        }
        case 3: {
            std::string cp = actions::RandomScene(s.rouletteIncludeMain, s.rouletteIncludeDlc);
            if (!cp.empty()) actions::LoadCheckpoint(cp);
            break;
        }
        default: break;
    }
}
}  // namespace

void OnGameFrame(UObject* viewportClient) {
    if (!Ready()) return;
    auto t0 = std::chrono::steady_clock::now();
    uint64_t now = NowMs();
    float dt = g_lastFrameMs ? static_cast<float>(now - g_lastFrameMs) / 1000.f : 0.f;
    g_lastFrameMs = now;
    if (dt > 0.25f) dt = 0.25f;  // hitches, loading screens
    ++g_frame;

    // Without a consistent view of the game objects nothing else can run.
    if (!RunPhase(g_pWorld, [&] { RefreshWorld(viewportClient); })) return;
    const World& w = W();
    if (w.worldInfo != g_lastWorldInfo || w.pc != g_lastPc) {
        g_lastWorldInfo = w.worldInfo;
        g_lastPc = w.pc;
        if (w.worldInfo && w.pc) RunPhase(g_pLevel, [] { OnWorldChanged(); });
    }

    RunPhase(g_pActions, [] { RunQueued(); });
    ModState s = state::Shared();

    if (w.pc) {
        RunPhase(g_pPlayer, [&] { ApplyPlayerFeatures(s); });
        RunPhase(g_pVisuals, [&] { ApplyVisualFeatures(s); });
        RunPhase(g_pWorldOpts, [&] { ApplyWorldFeatures(s, dt); });
        RunPhase(g_pEnemies, [&] { ApplyEnemyFeatures(s, dt); });
        RunPhase(g_pRoulette, [&] { Roulette(s, dt); });
        RunPhase(g_pPerf, [&] { perf::Update(static_cast<perf::Level>(Clamp(s.perfLevel, 0, 2))); });
    }
    RunPhase(g_pPause, [&] { ApplyMenuPause(s.pauseWhileMenuOpen && input::MenuOpen()); });
    RunPhase(g_pPlugins, [] { plugins::OnGameFrame(); });

    Snapshot snap;
    snap.engineReady = true;
    snap.frame = g_frame;
    RunPhase(g_pSnapPlayer, [&] {
        snap.mapName = MapName();
        FillPlayerSnapshot(snap);
    });
    RunPhase(g_pSnapMenu, [&] { FillMenuState(snap); });
    RunPhase(g_pSnapEnemies, [&] { enemies::FillSnapshot(snap); });
    RunPhase(g_pEsp, [&] { CollectEsp(s, snap); });
    snap.engineStatus = guard::FaultCount() ? "running (recovered from errors - see the log)" : "running";
    state::PublishSnapshot(std::move(snap));

    if (now - g_lastPurge > 2000) {
        g_lastPurge = now;
        RunPhase(g_pPurge, [] { Overrides().Purge(); });
    }

    float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    LockGuard lock(g_statsLock);
    g_stats.frames = g_frame;
    g_stats.modMs = ms;
    g_stats.modMsAvg = g_stats.modMsAvg * 0.95f + ms * 0.05f;
}

FrameStats GetFrameStats() {
    LockGuard lock(g_statsLock);
    return g_stats;
}

}  // namespace omm::game
