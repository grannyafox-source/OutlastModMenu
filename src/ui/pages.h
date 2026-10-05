// Menu pages and the small widget helpers they share.
#pragma once

#include "../game/state.h"

#include "imgui.h"

#include <string>

namespace omm::ui {

struct Ctx {
    const game::Snapshot& snap;
    game::ModState& s;  // the UI's copy, published after every frame
};

// --- Pages -----------------------------------------------------------------------
void PagePlayer(Ctx& c);
void PageMovement(Ctx& c);
void PageCamera(Ctx& c);
void PageBatteries(Ctx& c);
void PageWorld(Ctx& c);
void PageEnemies(Ctx& c);
void PageCharacter(Ctx& c);
void PageVisuals(Ctx& c);
void PageEsp(Ctx& c);
void PagePerformance(Ctx& c);
void PageTeleport(Ctx& c);
void PageGuide(Ctx& c);
void PageMods(Ctx& c);
void PageIniTweaks(Ctx& c);
void PageSettings(Ctx& c);
void PageDiagnostics(Ctx& c);

// ESP overlay (drawn behind the menu, also while it is closed).
void DrawEsp(const game::Snapshot& snap, const game::ModState& s);

// --- Widgets -----------------------------------------------------------------------
void ApplyTheme(float scale);
void Help(const char* text);                 // "(?)" with a tooltip
void Hint(const char* text);                 // dimmed wrapped text
bool Toggle(const char* label, bool* v, const char* help = nullptr);
bool SliderF(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f", float reset = -12345.f,
             const char* help = nullptr);
bool SliderI(const char* label, int* v, int lo, int hi, int reset = -12345, const char* help = nullptr);
bool Button(const char* label, const ImVec2& size = ImVec2(0, 0));
bool DangerButton(const char* label);
void Status(bool ok, const char* okText, const char* badText);
void Heading(const char* text);
bool NeedsGame(const game::Snapshot& snap);  // shows a note and returns true when not in game
float Em(float n);                           // n * font size (DPI-aware layout)
const char* KeyName(int vk);

// Hotkeys (configurable, stored in config.ini [Hotkeys]).
enum class Hotkey {
    Menu = 0,
    Noclip,
    Freecam,
    GodMode,
    Esp,
    SavePos,
    LoadPos,
    Invisible,
    SlowMo,
    TeleportCrosshair,
    Count
};
const char* HotkeyName(Hotkey h);
int HotkeyKey(Hotkey h);
void SetHotkeyKey(Hotkey h, int vk);
void LoadHotkeys();
// Rebinding: the next key pressed in the game window is captured.
void BeginHotkeyCapture(int slot);
int CaptureSlot();       // -1 when not capturing
int TakeCapturedKey();   // -1 = nothing captured yet, 0 = cleared (Esc)

}  // namespace omm::ui
