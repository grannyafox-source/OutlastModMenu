// Example plugin for Outlast Mod Menu.
//
// Build it with the mod (CMake target omm_plugin_example) or on its own
// against sdk/omm_plugin_api.h and the same Dear ImGui version (1.91.9), then
// copy the DLL to <Outlast>\Binaries\Win64\OutlastModMenu\plugins.
//
// It adds a section to the menu's Mod Loader > Plugins tab that shows the
// player's health and has a button for a "moon jump".
#include "omm_plugin_api.h"

#include "imgui.h"

#include <cstdio>

namespace {
const OMM_Api* g_api = nullptr;
int g_health = -1;
bool g_moonJump = false;
bool g_moonApplied = false;
float g_originalJumpZ = 0.f;

// Runs on the game thread every frame.
void OnFrame(void*) {
    void* hero = g_api->Hero();
    if (!hero) {
        g_health = -1;
        g_moonApplied = false;
        return;
    }
    int32_t hp = 0;
    if (g_api->GetInt(hero, "Health", &hp)) g_health = hp;

    // Pawn.JumpZ is the jump velocity; triple it while the option is on.
    if (g_moonJump && !g_moonApplied) {
        if (g_api->GetFloat(hero, "JumpZ", &g_originalJumpZ)) {
            g_api->SetFloat(hero, "JumpZ", g_originalJumpZ * 3.f);
            g_moonApplied = true;
        }
    } else if (!g_moonJump && g_moonApplied) {
        g_api->SetFloat(hero, "JumpZ", g_originalJumpZ);
        g_moonApplied = false;
    }
}

void Heal(void*) {
    if (void* hero = g_api->Hero()) g_api->SetInt(hero, "Health", 100);
}

// Runs on the render thread inside the mod's menu.
void DrawMenu(void*) {
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(g_api->GetImGuiContext()));
    void* allocFn = nullptr;
    void* freeFn = nullptr;
    void* user = nullptr;
    g_api->GetImGuiAllocatorFunctions(&allocFn, &freeFn, &user);
    ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(allocFn), reinterpret_cast<ImGuiMemFreeFunc>(freeFn),
                                 user);

    ImGui::TextUnformatted("Hello from the example plugin!");
    if (g_health >= 0) ImGui::Text("Player health: %d", g_health);
    else ImGui::TextDisabled("Not in game.");
    ImGui::Checkbox("Moon jump (3x jump velocity)", &g_moonJump);
    if (ImGui::Button("Heal (runs on the game thread)")) g_api->QueueOnGameThread(&Heal, nullptr);
}
}  // namespace

extern "C" __declspec(dllexport) const char* OMM_PluginName(void) { return "Example plugin"; }

extern "C" __declspec(dllexport) int OMM_PluginInit(const OMM_Api* api) {
    if (!api || api->version < 1 || api->size < sizeof(OMM_Api)) return 1;
    g_api = api;
    api->RegisterFrameCallback(&OnFrame, nullptr);
    api->RegisterMenuSection("Example plugin", &DrawMenu, nullptr);
    api->Log("Example plugin initialised");
    return 0;
}
