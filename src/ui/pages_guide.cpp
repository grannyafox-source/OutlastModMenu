// Hints & Guide page: survival tips, achievements, chapter objectives and the
// player's own key bindings (read from OLInput.ini).
#include "pages.h"

#include "../core/ini.h"
#include "../core/paths.h"
#include "../core/strutil.h"
#include "../game/data/guide.h"

#include <map>

namespace omm::ui {

namespace {
char g_search[64] = "";
int g_section = 0;

struct Binding {
    std::string action;
    std::string keys;
};
std::vector<Binding> g_bindings;
bool g_bindingsLoaded = false;

const std::map<std::string, const char*>& ActionNames() {
    static const std::map<std::string, const char*> names = {
        {"OLA_MoveForward", "Move forward"},
        {"OLA_MoveBackward", "Move backward"},
        {"OLA_StrafeLeft", "Move left"},
        {"OLA_StrafeRight", "Move right"},
        {"OLA_Run", "Run"},
        {"OLA_Jump", "Jump / vault / climb"},
        {"OLA_Crouch", "Crouch (hold)"},
        {"OLA_CrouchToggle", "Crouch (toggle)"},
        {"OLA_Use", "Use / open / pick up / hide"},
        {"OLA_ToggleCamcorder", "Raise the camcorder"},
        {"OLA_ToggleNightVision", "Night vision"},
        {"OLA_Reload", "Replace the battery"},
        {"OLA_LeanLeft", "Lean / peek left (look back while running)"},
        {"OLA_LeanRight", "Lean / peek right"},
        {"OLA_ZoomImpulseIn", "Zoom in"},
        {"OLA_ZoomImpulseOut", "Zoom out"},
        {"OLA_ShowMenu", "Pause menu"},
        {"OLA_ShowTabMenu", "Objectives"},
        {"OLA_ShowRecordingMenu", "Notes (recordings)"},
        {"OLA_ShowEvidenceMenu", "Documents"},
    };
    return names;
}

std::string Field(const std::string& line, const char* field) {
    std::string key = std::string(field) + "=";
    size_t p = line.find(key);
    if (p == std::string::npos) return std::string();
    p += key.size();
    if (p < line.size() && line[p] == '"') {
        size_t e = line.find('"', p + 1);
        return e == std::string::npos ? std::string() : line.substr(p + 1, e - p - 1);
    }
    size_t e = line.find_first_of(",)", p);
    return line.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

void LoadBindings() {
    g_bindingsLoaded = true;
    g_bindings.clear();
    IniDocument doc;
    if (!doc.Load(paths::ConfigFile("OLInput.ini"))) return;
    std::map<std::string, std::vector<std::string>> keysFor;
    for (const std::string& b : doc.GetAll("OLGame.OLPlayerInput", "Bindings")) {
        std::string name = Field(b, "Name"), cmd = Field(b, "Command");
        if (name.empty() || !str::IStartsWith(cmd, "OLA_")) continue;
        if (str::IStartsWith(name, "Xbox") || str::IStartsWith(name, "Gamepad")) continue;
        std::string mods;
        if (Field(b, "Control") == "True") mods += "Ctrl+";
        if (Field(b, "Shift") == "True") mods += "Shift+";
        if (Field(b, "Alt") == "True") mods += "Alt+";
        keysFor[cmd].push_back(mods + name);
    }
    for (const auto& kv : ActionNames()) {
        auto it = keysFor.find(kv.first);
        if (it == keysFor.end()) continue;
        g_bindings.push_back({kv.second, str::Join(it->second, ", ")});
    }
}

void DrawText(const char* text) {
    for (const std::string& line : str::Split(text, '\n', false)) {
        if (str::IStartsWith(line, "- ")) ImGui::BulletText("%s", line.substr(2).c_str());
        else if (!line.empty()) ImGui::TextWrapped("%s", line.c_str());
    }
}

bool Matches(const data::GuideEntry& e) {
    return !g_search[0] || str::IContains(e.heading, g_search) || str::IContains(e.text, g_search);
}
}  // namespace

void PageGuide(Ctx&) {
    const auto& guide = data::Guide();
    ImGui::SetNextItemWidth(Em(18));
    ImGui::InputTextWithHint("##search", "Search tips (e.g. Chris Walker, batteries, Trager)", g_search, sizeof(g_search));
    ImGui::Separator();

    if (g_search[0]) {
        int found = 0;
        for (const auto& sec : guide)
            for (const auto& e : sec.entries) {
                if (!Matches(e)) continue;
                ++found;
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.4f, 1.f), "%s > %s", sec.title, e.heading);
                ImGui::PushTextWrapPos(0.f);
                DrawText(e.text);
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
            }
        if (!found) ImGui::TextDisabled("Nothing found.");
        return;
    }

    ImGui::BeginChild("##sections", ImVec2(Em(11), 0), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(guide.size()); ++i)
        if (ImGui::Selectable(guide[i].title, g_section == i)) g_section = i;
    if (ImGui::Selectable("Your controls", g_section == static_cast<int>(guide.size()))) g_section = static_cast<int>(guide.size());
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##text");
    ImGui::PushTextWrapPos(0.f);
    if (g_section < static_cast<int>(guide.size())) {
        for (const auto& e : guide[g_section].entries) {
            ImGui::SeparatorText(e.heading);
            DrawText(e.text);
            ImGui::Spacing();
        }
    } else {
        if (!g_bindingsLoaded) LoadBindings();
        ImGui::SeparatorText("Your key bindings (from OLInput.ini)");
        if (g_bindings.empty()) {
            ImGui::TextDisabled("Could not read OLInput.ini.");
        } else if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            for (const Binding& b : g_bindings) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(b.action.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(b.keys.c_str());
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Reload")) LoadBindings();
        ImGui::SeparatorText("Mod hotkeys");
        for (int i = 0; i < static_cast<int>(Hotkey::Count); ++i) {
            Hotkey h = static_cast<Hotkey>(i);
            ImGui::BulletText("%s: %s", HotkeyName(h), KeyName(HotkeyKey(h)));
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

}  // namespace omm::ui
