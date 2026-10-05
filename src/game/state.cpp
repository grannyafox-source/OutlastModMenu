#include "state.h"

#include "../core/settings.h"
#include "../core/strutil.h"
#include "../core/sync.h"

#include <deque>

namespace omm::game {

const char* EspCategoryName(EspCategory c) {
    switch (c) {
        case EspCategory::Enemy: return "Enemies";
        case EspCategory::Battery: return "Batteries";
        case EspCategory::Document: return "Documents";
        case EspCategory::Recording: return "Recording spots";
        case EspCategory::KeyItem: return "Keys & items";
        case EspCategory::HidingSpot: return "Lockers & beds";
        case EspCategory::Checkpoint: return "Checkpoints";
        case EspCategory::Door: return "Doors";
        default: return "?";
    }
}

void InitEspDefaults(ModState& s) {
    auto set = [&](EspCategory c, bool on, float r, float g, float b) {
        EspStyle& st = s.esp[static_cast<int>(c)];
        st.enabled = on;
        st.color[0] = r;
        st.color[1] = g;
        st.color[2] = b;
        st.color[3] = 1.f;
    };
    set(EspCategory::Enemy, true, 1.0f, 0.25f, 0.25f);
    set(EspCategory::Battery, true, 0.35f, 1.0f, 0.35f);
    set(EspCategory::Document, true, 1.0f, 0.85f, 0.3f);
    set(EspCategory::Recording, true, 0.3f, 0.75f, 1.0f);
    set(EspCategory::KeyItem, true, 1.0f, 0.5f, 1.0f);
    set(EspCategory::HidingSpot, false, 0.75f, 0.75f, 0.75f);
    set(EspCategory::Checkpoint, false, 1.0f, 1.0f, 1.0f);
    set(EspCategory::Door, false, 0.6f, 0.45f, 0.3f);
}

namespace {
Mutex g_initLock;
Mutex g_stateLock;
ModState g_ui;
ModState g_shared;
volatile bool g_init = false;

Mutex g_snapLock;
Snapshot g_snap;

Mutex g_queueLock;
std::deque<std::function<void()>> g_queue;

Mutex g_toastLock;
std::deque<Toast> g_toasts;

void EnsureInit() {
    if (g_init) return;
    LockGuard lock(g_initLock);
    if (g_init) return;
    InitEspDefaults(g_ui);
    g_shared = g_ui;
    g_init = true;
}

// Fields persisted in config.ini. Cheat toggles are deliberately not
// persisted so the game always starts unmodified.
#define OMM_PERSISTED_FLOATS(X)                                                                          \
    X(regenDelay) X(regenRate) X(speedMultiplier) X(walkSpeed) X(runSpeed) X(crouchSpeed) X(waterWalkSpeed) \
    X(waterRunSpeed) X(limpWalkSpeed) X(hobbleWalkSpeed) X(hobbleRunSpeed) X(jumpMultiplier)             \
    X(gravityMultiplier) X(noclipSpeed) X(fov) X(runFov) X(camcorderMaxFov) X(batteryDuration)            \
    X(nightVisionRange) X(nightVisionBrightness) X(gameSpeed) X(enemyTimeScale) X(enemySpeedMultiplier)   \
    X(enemyDamageMultiplier) X(gamma) X(shadowsMul) X(midtonesMul) X(highlightsMul) X(desaturationMul)    \
    X(darkLightRadius) X(darkLightBrightness) X(fpsLimit) X(espMaxDistance) X(hobbleIntensity)            \
    X(hordeInterval) X(uiScale)

#define OMM_PERSISTED_INTS(X) X(maxHealth) X(batteryCount) X(maxBatteries) X(walkingStyle) X(hordeMaxEnemies) \
    X(hordeEnemyType) X(menuButtonCorner)

#define OMM_PERSISTED_BOOLS(X)                                                                              \
    X(gammaOverride) X(brightnessOverride) X(darkLightOverride) X(noFilmGrain) X(noVignette) X(noHurtEffect) \
    X(tintOverride) X(hideCrosshair) X(fpsLimitOverride) X(espEnabled) X(espShowDistance) X(espShowLabels)   \
    X(espTracers) X(espHideCollected) X(espOffscreenArrows) X(autoApplyModelSwaps) X(pauseWhileMenuOpen)     \
    X(showModMenuButton) X(notifications) X(fovOverride)
}  // namespace

namespace state {

ModState& Ui() {
    EnsureInit();
    return g_ui;
}

void Publish() {
    EnsureInit();
    LockGuard lock(g_stateLock);
    g_shared = g_ui;
}

ModState Shared() {
    EnsureInit();
    LockGuard lock(g_stateLock);
    return g_shared;
}

void Load() {
    EnsureInit();
    Settings& cfg = Settings::Get();
    ModState& s = g_ui;
#define X(name) s.name = cfg.GetFloat("State", #name, s.name);
    OMM_PERSISTED_FLOATS(X)
#undef X
#define X(name) s.name = cfg.GetInt("State", #name, s.name);
    OMM_PERSISTED_INTS(X)
#undef X
#define X(name) s.name = cfg.GetBool("State", #name, s.name);
    OMM_PERSISTED_BOOLS(X)
#undef X
    for (int i = 0; i < static_cast<int>(EspCategory::Count); ++i) {
        std::string key = str::Format("Esp%d", i);
        std::string v = cfg.GetString("State", key.c_str(), "");
        std::vector<std::string> parts = str::Split(v, ',');
        if (parts.size() == 5) {
            bool on = false;
            if (str::ParseBool(parts[0], on)) s.esp[i].enabled = on;
            for (int c = 0; c < 4; ++c) str::ParseFloat(parts[c + 1], s.esp[i].color[c]);
        }
    }
    std::vector<std::string> tint = str::Split(cfg.GetString("State", "tint", ""), ',');
    if (tint.size() == 4)
        for (int c = 0; c < 4; ++c) str::ParseFloat(tint[c], s.tint[c]);
    s.uiScale = Clamp(s.uiScale, 0.6f, 2.0f);
    Publish();
}

void Save() {
    Settings& cfg = Settings::Get();
    const ModState& s = g_ui;
#define X(name) cfg.SetFloat("State", #name, s.name);
    OMM_PERSISTED_FLOATS(X)
#undef X
#define X(name) cfg.SetInt("State", #name, s.name);
    OMM_PERSISTED_INTS(X)
#undef X
#define X(name) cfg.SetBool("State", #name, s.name);
    OMM_PERSISTED_BOOLS(X)
#undef X
    for (int i = 0; i < static_cast<int>(EspCategory::Count); ++i) {
        const EspStyle& e = s.esp[i];
        cfg.SetString("State", str::Format("Esp%d", i).c_str(),
                      str::Format("%s,%.3f,%.3f,%.3f,%.3f", e.enabled ? "true" : "false", e.color[0], e.color[1],
                                  e.color[2], e.color[3]));
    }
    cfg.SetString("State", "tint", str::Format("%.3f,%.3f,%.3f,%.3f", s.tint[0], s.tint[1], s.tint[2], s.tint[3]));
}

void PublishSnapshot(Snapshot&& s) {
    LockGuard lock(g_snapLock);
    g_snap = std::move(s);
}

Snapshot GetSnapshot() {
    LockGuard lock(g_snapLock);
    return g_snap;
}

}  // namespace state

void Enqueue(std::function<void()> fn) {
    LockGuard lock(g_queueLock);
    if (g_queue.size() < 512) g_queue.push_back(std::move(fn));
}

void RunQueued() {
    std::deque<std::function<void()>> work;
    {
        LockGuard lock(g_queueLock);
        work.swap(g_queue);
    }
    for (auto& fn : work) fn();
}

void Notify(const std::string& text, float seconds) {
    LockGuard lock(g_toastLock);
    g_toasts.push_back({text, NowMs() + static_cast<uint64_t>(seconds * 1000.f)});
    while (g_toasts.size() > 6) g_toasts.pop_front();
}

std::vector<Toast> ActiveToasts() {
    LockGuard lock(g_toastLock);
    uint64_t now = NowMs();
    for (auto it = g_toasts.begin(); it != g_toasts.end();) it = it->expiresMs < now ? g_toasts.erase(it) : it + 1;
    return std::vector<Toast>(g_toasts.begin(), g_toasts.end());
}

}  // namespace omm::game
