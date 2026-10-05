// Settings and Diagnostics pages.
#include "pages.h"

#include "../core/log.h"
#include "../core/paths.h"
#include "../core/settings.h"
#include "../core/strutil.h"
#include "../game/actions.h"
#include "../game/frame.h"
#include "../game/inspector.h"
#include "../game/modloader.h"
#include "../render/overlay.h"
#include "../ue3/call.h"
#include "../ue3/engine.h"
#include "../ue3/hooks.h"

namespace omm::ui {

using namespace game;

namespace {
int g_captureFor = -1;
char g_console[256] = "";
char g_inspectName[128] = "Hero";
char g_inspectClass[64] = "";
bool g_logAutoScroll = true;

void ResetCheats(ModState& s) {
    ModState fresh;
    InitEspDefaults(fresh);
    // Keep the menu's own preferences.
    fresh.uiScale = s.uiScale;
    fresh.pauseWhileMenuOpen = s.pauseWhileMenuOpen;
    fresh.showModMenuButton = s.showModMenuButton;
    fresh.menuButtonCorner = s.menuButtonCorner;
    fresh.notifications = s.notifications;
    s = fresh;
}
}  // namespace

void PageSettings(Ctx& c) {
    ModState& s = c.s;
    Heading("Menu");
    SliderF("Menu size", &s.uiScale, 0.6f, 2.f, "%.2fx", 1.f);
    Toggle("Pause the game while the menu is open", &s.pauseWhileMenuOpen);
    Toggle("Show the MOD MENU button on the game's menus", &s.showModMenuButton,
           "Adds a button to Outlast's main menu, pause menu and Options screen.");
    const char* corners[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
    ImGui::Combo("Button position", &s.menuButtonCorner, corners, 4);
    Toggle("On-screen notifications", &s.notifications);

    Heading("Hotkeys");
    int captured = TakeCapturedKey();
    if (captured >= 0 && g_captureFor >= 0) {
        SetHotkeyKey(static_cast<Hotkey>(g_captureFor), captured);
        g_captureFor = -1;
    }
    if (ImGui::BeginTable("##hotkeys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (int i = 0; i < static_cast<int>(Hotkey::Count); ++i) {
            Hotkey h = static_cast<Hotkey>(i);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(HotkeyName(h));
            ImGui::TableNextColumn();
            bool waiting = g_captureFor == i && CaptureSlot() >= 0;
            if (ImGui::Button(waiting ? "Press a key... (Esc = none)" : KeyName(HotkeyKey(h)), ImVec2(Em(12), 0))) {
                g_captureFor = i;
                BeginHotkeyCapture(i);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    Hint("F1 always opens the menu as well. Hotkeys work in game and in menus; they are ignored while you type "
         "in a text box.");

    Heading("Reset");
    if (ImGui::Button("Turn every cheat off")) {
        ResetCheats(s);
        Notify("All mod options reset");
    }
    Help("Restores the game's own values for everything the mod changed (INI tweaks are separate).");
    ImGui::SameLine();
    if (ImGui::Button("Save settings now")) {
        state::Save();
        Settings::Get().Save();
    }

    Heading("About");
    ImGui::Text("%s %s", OMM_NAME, OMM_VERSION_STRING);
    ImGui::TextDisabled("Built with Dear ImGui %s and MinHook.", IMGUI_VERSION);
    Hint("Settings are stored in OutlastModMenu\\config.ini next to the game executable. Read docs\\README.md "
         "for every feature and docs\\TROUBLESHOOTING.md if something does not work.");
}

void PageDiagnostics(Ctx& c) {
    const Snapshot& snap = c.snap;
    if (ImGui::BeginTabBar("##diag")) {
        if (ImGui::BeginTabItem("Status")) {
            Status(snap.engineReady, "Engine found", "Engine not ready");
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", snap.engineStatus.c_str());
            Status(ue3::ProcessEventReady(), str::Format("ProcessEvent: vtable slot %d", ue3::ProcessEventIndex()).c_str(),
                   "ProcessEvent not found yet (needs the first frame)");
            ImGui::Text("Frame hook calls: %llu%s", static_cast<unsigned long long>(ue3::HookCalls()),
                        ue3::ProcessInternalHookActive() ? " (fallback hook)" : "");
            if (ue3::Ready()) {
                ImGui::Text("Names: %d   Objects: %d", ue3::NamesNum(), ue3::ObjectsNum());
                ImGui::TextWrapped("Layout: %s", ue3::L().Describe().c_str());
            }
            FrameStats fs = GetFrameStats();
            ImGui::Text("Overlay: %s, %.0f FPS, mod cost %.2f ms/frame", render::BackendName(), render::FrameRate(),
                        fs.modMsAvg);
            ImGui::Text("Game menu: %s", snap.menuView.empty() ? "-" : snap.menuView.c_str());
            ImGui::Separator();
            ImGui::TextWrapped("Mod folder: %s", paths::ModDir().c_str());
            ImGui::TextWrapped("Game: %s", paths::GameRoot().c_str());
            ImGui::TextWrapped("Config: %s", paths::UserConfigDir().c_str());
            ImGui::TextWrapped("Content: %s", paths::CookedDir().c_str());
            if (ImGui::Button("Open mod folder")) mods::OpenFolder(paths::ModDir());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Log")) {
            std::vector<std::string> lines = log::Recent(400);
            if (ImGui::Button("Copy to clipboard")) ImGui::SetClipboardText(str::Join(lines, "\n").c_str());
            ImGui::SameLine();
            ImGui::Checkbox("Auto-scroll", &g_logAutoScroll);
            ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
            for (const std::string& l : lines) ImGui::TextUnformatted(l.c_str());
            if (g_logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.f);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Console")) {
            Hint("Runs Unreal Engine console commands, e.g. 'slomo 0.5', 'ghost', 'walk', 'open <map>'. "
                 "Output appears below.");
            static bool refocus = false;
            if (refocus) {
                ImGui::SetKeyboardFocusHere();
                refocus = false;
            }
            ImGui::SetNextItemWidth(-Em(5));
            bool run = ImGui::InputText("##cmd", g_console, sizeof(g_console), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            run |= ImGui::Button("Run");
            if (run && g_console[0]) {
                std::string cmd = g_console;
                Enqueue([cmd] { actions::RunConsoleCommand(cmd); });
                g_console[0] = 0;
                refocus = true;
            }
            ImGui::BeginChild("##out", ImVec2(0, 0), ImGuiChildFlags_Borders);
            for (const std::string& l : actions::ConsoleLog()) ImGui::TextUnformatted(l.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.f);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Object inspector")) {
            Hint("For modders: look at live game objects. Shortcuts: Hero, PC, WorldInfo, Game, HUD, CheatManager, "
                 "Engine, LocalPlayer. Otherwise an object name, a dotted path, or a class name below.");
            ImGui::SetNextItemWidth(Em(14));
            ImGui::InputText("Object", g_inspectName, sizeof(g_inspectName));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(Em(10));
            ImGui::InputText("Class (optional)", g_inspectClass, sizeof(g_inspectClass));
            if (ImGui::Button("Show properties")) inspector::InspectObject(g_inspectName, g_inspectClass);
            ImGui::SameLine();
            if (ImGui::Button("List instances of class")) inspector::ListInstances(g_inspectClass[0] ? g_inspectClass : g_inspectName);
            ImGui::SameLine();
            if (ImGui::Button("List functions of class")) inspector::ListFunctions(g_inspectClass[0] ? g_inspectClass : g_inspectName);
            inspector::Result r = inspector::Last();
            ImGui::SeparatorText(r.title.empty() ? "Result" : r.title.c_str());
            ImGui::BeginChild("##insp", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
            for (const std::string& l : r.lines) ImGui::TextUnformatted(l.c_str());
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

}  // namespace omm::ui
