#include "ui.h"

#include "pages.h"

#include "../core/common.h"
#include "../core/log.h"
#include "../core/settings.h"
#include "../core/strutil.h"
#include "../core/sync.h"
#include "../game/actions.h"
#include "../game/state.h"
#include "../render/input.h"
#include "../render/overlay.h"

#include <atomic>
#include <cstring>
#include <vector>

namespace omm::ui {

namespace {
std::atomic<bool> g_open{false};
std::atomic<bool> g_wantText{false};
std::atomic<int> g_captureSlot{-1};  // hotkey being rebound (Settings page)
std::atomic<int> g_capturedVk{0};
std::atomic<uint64_t> g_captureEndedMs{0};

Mutex g_hotkeyLock;
std::vector<int> g_pendingHotkeys;

int g_page = 0;
float g_themeScale = 0.f;
uint64_t g_lastSaveCheck = 0;
alignas(game::ModState) unsigned char g_lastSaved[sizeof(game::ModState)];
bool g_haveLastSaved = false;
bool g_firstOpen = true;

struct PageDef {
    const char* name;
    void (*draw)(Ctx&);
    bool separatorBefore;
};
const PageDef kPages[] = {
    {"Player", PagePlayer, false},
    {"Movement", PageMovement, false},
    {"Camera", PageCamera, false},
    {"Batteries", PageBatteries, false},
    {"World & AI", PageWorld, true},
    {"Enemies", PageEnemies, false},
    {"Character", PageCharacter, false},
    {"Visuals", PageVisuals, true},
    {"ESP", PageEsp, false},
    {"Performance", PagePerformance, false},
    {"Teleport", PageTeleport, true},
    {"Hints & Guide", PageGuide, false},
    {"Mod Loader", PageMods, true},
    {"INI Tweaks", PageIniTweaks, false},
    {"Settings", PageSettings, true},
    {"Diagnostics", PageDiagnostics, false},
};
constexpr int kPageCount = static_cast<int>(sizeof(kPages) / sizeof(kPages[0]));

void SetOpen(bool open) {
    g_open = open;
    input::SetMenuOpen(open);
    if (!open) game::state::Save();
}

void RunHotkey(Hotkey h) {
    using namespace game;
    ModState& s = state::Ui();
    switch (h) {
        case Hotkey::Menu: SetOpen(!g_open); break;
        case Hotkey::Noclip: Enqueue([] { actions::ToggleNoclip(); }); break;
        case Hotkey::Freecam: Enqueue([] { actions::ToggleFreecam(false); }); break;
        case Hotkey::GodMode:
            s.godMode = !s.godMode;
            Notify(s.godMode ? "God mode ON" : "God mode OFF");
            break;
        case Hotkey::Esp:
            s.espEnabled = !s.espEnabled;
            Notify(s.espEnabled ? "ESP ON" : "ESP OFF");
            break;
        case Hotkey::SavePos: Enqueue([] { actions::QuickSavePosition(); }); break;
        case Hotkey::LoadPos: Enqueue([] { actions::QuickLoadPosition(); }); break;
        case Hotkey::Invisible:
            s.invisible = !s.invisible;
            Notify(s.invisible ? "Invisible to enemies" : "Visible to enemies");
            break;
        case Hotkey::SlowMo:
            if (s.gameSpeedOverride && s.gameSpeed < 0.99f) {
                s.gameSpeedOverride = false;
                Notify("Normal speed");
            } else {
                s.gameSpeedOverride = true;
                s.gameSpeed = 0.3f;
                Notify("Slow motion");
            }
            break;
        case Hotkey::TeleportCrosshair: Enqueue([] { actions::TeleportToCrosshair(); }); break;
        default: break;
    }
}

void ProcessHotkeys() {
    std::vector<int> pending;
    {
        LockGuard lock(g_hotkeyLock);
        pending.swap(g_pendingHotkeys);
    }
    for (int h : pending) RunHotkey(static_cast<Hotkey>(h));
}

void DrawToasts() {
    std::vector<game::Toast> toasts = game::ActiveToasts();
    if (toasts.empty() || !game::state::Ui().notifications) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 display = ImGui::GetIO().DisplaySize;
    float y = display.y * 0.12f;
    for (const game::Toast& t : toasts) {
        ImVec2 sz = ImGui::CalcTextSize(t.text.c_str());
        ImVec2 pad(Em(0.8f), Em(0.35f));
        ImVec2 a(display.x * 0.5f - sz.x * 0.5f - pad.x, y);
        ImVec2 b(display.x * 0.5f + sz.x * 0.5f + pad.x, y + sz.y + pad.y * 2);
        dl->AddRectFilled(a, b, IM_COL32(15, 12, 12, 215), Em(0.3f));
        dl->AddRect(a, b, IM_COL32(150, 25, 25, 230), Em(0.3f), 0, 1.5f);
        dl->AddText(ImVec2(a.x + pad.x, a.y + pad.y), IM_COL32(235, 230, 220, 255), t.text.c_str());
        y = b.y + Em(0.3f);
    }
}

// Button drawn on top of the game's own main / pause / options menu.
void DrawGameMenuButton(const game::Snapshot& snap, const game::ModState& s) {
    bool show = s.showModMenuButton && !g_open &&
                (snap.menu == game::GameMenu::MainMenu || snap.menu == game::GameMenu::Pause ||
                 snap.menu == game::GameMenu::Options);
    if (!show) {
        input::SetOverlayWantsMouse(false);
        return;
    }
    ImVec2 display = ImGui::GetIO().DisplaySize;
    const float margin = Em(1.2f);
    int corner = Clamp(s.menuButtonCorner, 0, 3);
    ImVec2 pos(corner & 1 ? display.x - margin : margin, corner & 2 ? display.y - margin : margin);
    ImVec2 pivot(corner & 1 ? 1.f : 0.f, corner & 2 ? 1.f : 0.f);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    bool hovered = false;
    if (ImGui::Begin("##omm_menu_button", nullptr, flags)) {
        bool options = snap.menu == game::GameMenu::Options;
        std::string label = options ? std::string("MOD MENU  -  open the mod settings") : std::string("MOD MENU");
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Em(1.f), Em(0.5f)));
        if (ImGui::Button(label.c_str())) SetOpen(true);
        ImGui::PopStyleVar();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", KeyName(HotkeyKey(Hotkey::Menu)));
        hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    }
    ImGui::End();
    input::SetOverlayWantsMouse(hovered);
}

void DrawStatusBar(const game::Snapshot& snap) {
    ImGui::Separator();
    if (!snap.engineReady) {
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0.3f, 1.f), "Engine: %s", snap.engineStatus.empty() ? "starting..." : snap.engineStatus.c_str());
    } else {
        ImGui::TextDisabled("%s | %.0f FPS | %s%s%s", render::BackendName(), render::FrameRate(),
                            snap.mapName.empty() ? "menu" : snap.mapName.c_str(), snap.checkpoint.empty() ? "" : " @ ",
                            snap.checkpoint.c_str());
    }
}

void DrawMainWindow(Ctx& c) {
    ImVec2 display = ImGui::GetIO().DisplaySize;
    if (g_firstOpen) {
        g_firstOpen = false;
        ImVec2 size(std::min(Em(56), display.x * 0.92f), std::min(Em(38), display.y * 0.88f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(Em(30), Em(20)), ImVec2(display.x, display.y));
    bool open = true;
    std::string title = str::Format("%s %s   [%s to close]###omm_main", OMM_NAME, OMM_VERSION_STRING,
                                    KeyName(HotkeyKey(Hotkey::Menu)));
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse)) {
        float statusH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("##nav", ImVec2(Em(10.5f), -statusH), ImGuiChildFlags_Borders);
        for (int i = 0; i < kPageCount; ++i) {
            if (kPages[i].separatorBefore) ImGui::Separator();
            if (ImGui::Selectable(kPages[i].name, g_page == i)) g_page = i;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##page", ImVec2(0, -statusH), ImGuiChildFlags_Borders);
        ImGui::PushItemWidth(Em(16));
        ImGui::PushID(g_page);
        kPages[g_page].draw(c);
        ImGui::PopID();
        ImGui::PopItemWidth();
        ImGui::EndChild();
        DrawStatusBar(c.snap);
    }
    ImGui::End();
    if (!open) SetOpen(false);
}

void SaveIfChanged() {
    uint64_t now = NowMs();
    if (now - g_lastSaveCheck < 500) return;
    g_lastSaveCheck = now;
    const game::ModState& s = game::state::Ui();
    if (g_haveLastSaved && std::memcmp(g_lastSaved, &s, sizeof(s)) == 0) return;
    std::memcpy(g_lastSaved, &s, sizeof(s));
    if (g_haveLastSaved) game::state::Save();
    g_haveLastSaved = true;
}
}  // namespace

void OpenMenu(bool open) { SetOpen(open); }
bool IsMenuOpen() { return g_open; }
int PageCount() { return kPageCount; }
void SelectPage(int index) { g_page = Clamp(index, 0, kPageCount - 1); }

void Setup() {
    LoadHotkeys();
    float scale = game::state::Ui().uiScale;
    ApplyTheme(scale);
    g_themeScale = scale;
    game::Notify(str::Format("Outlast Mod Menu loaded - press %s", KeyName(HotkeyKey(Hotkey::Menu))), 6.f);
}

void Frame() {
    ProcessHotkeys();
    game::ModState& s = game::state::Ui();
    if (s.uiScale != g_themeScale && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        s.uiScale = Clamp(s.uiScale, 0.6f, 2.0f);
        ApplyTheme(s.uiScale);
        g_themeScale = s.uiScale;
    }
    game::Snapshot snap = game::state::GetSnapshot();
    Ctx c{snap, s};

    if (s.espEnabled && snap.inGame && snap.menu == game::GameMenu::None) DrawEsp(snap, s);
    if (g_open) {
        // Esc closes the menu unless it is needed by a text box or popup.
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && CaptureSlot() < 0 &&
            NowMs() - g_captureEndedMs > 500) {
            SetOpen(false);
        }
    }
    if (g_open) DrawMainWindow(c);
    DrawGameMenuButton(snap, s);
    DrawToasts();

    g_wantText = ImGui::GetIO().WantTextInput;
    game::state::Publish();
    SaveIfChanged();
}

bool OnHotkey(int vk, bool down, bool repeat) {
    // Rebinding a hotkey from the Settings page.
    int slot = g_captureSlot;
    if (slot >= 0) {
        if (down && !repeat) {
            g_capturedVk = vk == 0x1B ? 0 : vk;  // Esc clears the binding
            g_captureSlot = -1;
            g_captureEndedMs = NowMs();
        }
        return true;
    }
    // Typing in a text box: only the menu key is still a hotkey.
    bool typing = g_open && g_wantText;
    for (int i = 0; i < static_cast<int>(Hotkey::Count); ++i) {
        Hotkey h = static_cast<Hotkey>(i);
        int key = HotkeyKey(h);
        bool match = key != 0 && vk == key;
        if (h == Hotkey::Menu && vk == 0x70) match = true;  // F1 always opens the menu
        if (!match) continue;
        if (typing && h != Hotkey::Menu) return false;
        if (down && !repeat) {
            LockGuard lock(g_hotkeyLock);
            g_pendingHotkeys.push_back(i);
        }
        return true;
    }
    return false;
}

// Used by the Settings page.
void BeginHotkeyCapture(int slot) {
    g_capturedVk = -1;
    g_captureSlot = slot;
}
int CaptureSlot() { return g_captureSlot; }
int TakeCapturedKey() { return g_capturedVk.exchange(-1); }

}  // namespace omm::ui
