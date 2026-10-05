#include "pages.h"

#include "../core/settings.h"
#include "../core/strutil.h"

#include <cstdio>

namespace omm::ui {

float Em(float n) { return ImGui::GetFontSize() * n; }

void ApplyTheme(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 6.f;
    st.ChildRounding = 4.f;
    st.FrameRounding = 4.f;
    st.PopupRounding = 4.f;
    st.GrabRounding = 3.f;
    st.TabRounding = 4.f;
    st.ScrollbarRounding = 6.f;
    st.WindowBorderSize = 1.f;
    st.FrameBorderSize = 0.f;
    st.WindowPadding = ImVec2(12, 10);
    st.FramePadding = ImVec2(8, 4);
    st.ItemSpacing = ImVec2(8, 6);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.IndentSpacing = 18.f;
    st.ScrollbarSize = 14.f;
    st.SeparatorTextBorderSize = 2.f;
    st.WindowTitleAlign = ImVec2(0.02f, 0.5f);

    // Dark asylum grey with a blood-red accent and night-vision green for "on".
    ImVec4* c = st.Colors;
    const ImVec4 bg(0.07f, 0.07f, 0.075f, 0.97f);
    const ImVec4 panel(0.11f, 0.11f, 0.115f, 1.f);
    const ImVec4 red(0.62f, 0.09f, 0.09f, 1.f);
    const ImVec4 redHi(0.78f, 0.15f, 0.13f, 1.f);
    const ImVec4 redLo(0.42f, 0.07f, 0.07f, 1.f);
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.89f, 0.86f, 1.f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.48f, 1.f);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.09f, 0.095f, 0.6f);
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.095f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.28f, 0.10f, 0.10f, 0.6f);
    c[ImGuiCol_FrameBg] = panel;
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.12f, 0.12f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.27f, 0.13f, 0.13f, 1.f);
    c[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.05f, 0.05f, 1.f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.06f, 0.06f, 1.f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.05f, 0.05f, 0.8f);
    c[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.95f, 0.45f, 1.f);
    c[ImGuiCol_SliderGrab] = red;
    c[ImGuiCol_SliderGrabActive] = redHi;
    c[ImGuiCol_Button] = redLo;
    c[ImGuiCol_ButtonHovered] = red;
    c[ImGuiCol_ButtonActive] = redHi;
    c[ImGuiCol_Header] = ImVec4(0.35f, 0.08f, 0.08f, 0.8f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.50f, 0.10f, 0.10f, 0.9f);
    c[ImGuiCol_HeaderActive] = red;
    c[ImGuiCol_Separator] = ImVec4(0.35f, 0.12f, 0.12f, 0.8f);
    c[ImGuiCol_Tab] = ImVec4(0.18f, 0.07f, 0.07f, 1.f);
    c[ImGuiCol_TabHovered] = red;
    c[ImGuiCol_TabSelected] = ImVec4(0.45f, 0.08f, 0.08f, 1.f);
    c[ImGuiCol_ResizeGrip] = redLo;
    c[ImGuiCol_ResizeGripHovered] = red;
    c[ImGuiCol_ResizeGripActive] = redHi;
    c[ImGuiCol_PlotHistogram] = red;
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.08f, 0.08f, 1.f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.f, 1.f, 1.f, 0.03f);
    c[ImGuiCol_NavHighlight] = redHi;
    st.ScaleAllSizes(scale);
    ImGui::GetIO().FontGlobalScale = scale;
}

void Help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(Em(28));
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool Toggle(const char* label, bool* v, const char* help) {
    bool changed = ImGui::Checkbox(label, v);
    if (help) Help(help);
    return changed;
}

bool SliderF(const char* label, float* v, float lo, float hi, const char* fmt, float reset, const char* help) {
    ImGui::PushID(label);
    bool changed = false;
    if (reset != -12345.f) {
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - Em(2.2f));
        changed = ImGui::SliderFloat("##v", v, lo, hi, fmt);
        ImGui::SameLine(0, Em(0.3f));
        if (ImGui::SmallButton("R")) {
            *v = reset;
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to %s", str::FloatToIni(reset).c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
    } else {
        changed = ImGui::SliderFloat(label, v, lo, hi, fmt);
    }
    ImGui::PopID();
    if (help) Help(help);
    return changed;
}

bool SliderI(const char* label, int* v, int lo, int hi, int reset, const char* help) {
    ImGui::PushID(label);
    bool changed = false;
    if (reset != -12345) {
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - Em(2.2f));
        changed = ImGui::SliderInt("##v", v, lo, hi);
        ImGui::SameLine(0, Em(0.3f));
        if (ImGui::SmallButton("R")) {
            *v = reset;
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset to %d", reset);
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
    } else {
        changed = ImGui::SliderInt(label, v, lo, hi);
    }
    ImGui::PopID();
    if (help) Help(help);
    return changed;
}

bool Button(const char* label, const ImVec2& size) { return ImGui::Button(label, size); }

bool DangerButton(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.05f, 0.05f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.08f, 0.08f, 1.f));
    bool r = ImGui::Button(label);
    ImGui::PopStyleColor(2);
    return r;
}

void Status(bool ok, const char* okText, const char* badText) {
    ImGui::TextColored(ok ? ImVec4(0.45f, 0.95f, 0.45f, 1.f) : ImVec4(1.f, 0.55f, 0.3f, 1.f), "%s", ok ? okText : badText);
}

void Heading(const char* text) {
    ImGui::Spacing();
    ImGui::SeparatorText(text);
}

bool NeedsGame(const game::Snapshot& snap) {
    if (snap.inGame) return false;
    ImGui::TextColored(ImVec4(1.f, 0.7f, 0.3f, 1.f), "Load a game (or a chapter) to use these options.");
    return true;
}

const char* KeyName(int vk) {
    static char buf[16];
    if (vk == 0) return "(none)";
    if (vk >= 0x70 && vk <= 0x87) {  // F1..F24
        std::snprintf(buf, sizeof(buf), "F%d", vk - 0x6F);
        return buf;
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        buf[0] = static_cast<char>(vk);
        buf[1] = 0;
        return buf;
    }
    if (vk >= 0x60 && vk <= 0x69) {  // numpad
        std::snprintf(buf, sizeof(buf), "Num %d", vk - 0x60);
        return buf;
    }
    switch (vk) {
        case 0x2D: return "Insert";
        case 0x2E: return "Delete";
        case 0x24: return "Home";
        case 0x23: return "End";
        case 0x21: return "Page Up";
        case 0x22: return "Page Down";
        case 0x13: return "Pause";
        case 0x91: return "Scroll Lock";
        case 0xC0: return "` (tilde)";
        case 0x6A: return "Num *";
        case 0x6B: return "Num +";
        case 0x6D: return "Num -";
        case 0x6E: return "Num .";
        case 0x6F: return "Num /";
        case 0xBA: return ";";
        case 0xBB: return "=";
        case 0xBC: return ",";
        case 0xBD: return "-";
        case 0xBE: return ".";
        case 0xBF: return "/";
        case 0xDB: return "[";
        case 0xDC: return "\\";
        case 0xDD: return "]";
        case 0xDE: return "'";
        case 0x08: return "Backspace";
        case 0x09: return "Tab";
        case 0x0D: return "Enter";
        case 0x20: return "Space";
        default: break;
    }
    std::snprintf(buf, sizeof(buf), "Key 0x%02X", vk);
    return buf;
}

// --- Hotkeys --------------------------------------------------------------------------

namespace {
struct HotkeyDef {
    const char* id;
    const char* name;
    int defaultVk;
};
const HotkeyDef kHotkeys[] = {
    {"Menu", "Open / close the menu", 0x2D},      // Insert (F1 also works)
    {"Noclip", "Noclip (fly through walls)", 0x71},  // F2
    {"Freecam", "Free camera", 0x72},                // F3
    {"GodMode", "God mode", 0x73},                   // F4
    {"Esp", "ESP on/off", 0x74},                     // F5
    {"SavePos", "Save position", 0x75},              // F6
    {"LoadPos", "Return to saved position", 0x76},   // F7
    {"Invisible", "Invisibility", 0x78},             // F9
    {"SlowMo", "Slow motion", 0x79},                 // F10
    {"TeleportCrosshair", "Teleport to crosshair", 0},
};
volatile int g_keys[static_cast<int>(Hotkey::Count)] = {};
}  // namespace

const char* HotkeyName(Hotkey h) { return kHotkeys[static_cast<int>(h)].name; }
int HotkeyKey(Hotkey h) { return g_keys[static_cast<int>(h)]; }

void SetHotkeyKey(Hotkey h, int vk) {
    g_keys[static_cast<int>(h)] = vk;
    Settings::Get().SetInt("Hotkeys", kHotkeys[static_cast<int>(h)].id, vk);
}

void LoadHotkeys() {
    for (int i = 0; i < static_cast<int>(Hotkey::Count); ++i)
        g_keys[i] = Settings::Get().GetInt("Hotkeys", kHotkeys[i].id, kHotkeys[i].defaultVk);
}

}  // namespace omm::ui
