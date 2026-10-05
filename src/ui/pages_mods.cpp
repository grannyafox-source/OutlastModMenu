// Mod Loader and INI Tweaks pages.
#include "pages.h"
#include "ui.h"

#include "../core/paths.h"
#include "../core/strutil.h"
#include "../game/actions.h"
#include "../game/initweaks.h"
#include "../game/modloader.h"
#include "../game/plugins.h"

#include "imgui_internal.h"  // ImGuiItemFlags_MixedValue

#include <map>

namespace omm::ui {

using namespace game;

namespace {
// Mod loader caches (file I/O only on demand).
std::vector<mods::ContentMod> g_mods;
std::vector<mods::MapFile> g_maps;
mods::ReShadeInfo g_reshade;
bool g_modsScanned = false, g_mapsScanned = false, g_reshadeScanned = false;
char g_mapFilter[64] = "";
std::string g_modMessage;

// INI tweak state cache.
std::map<std::string, ini::TweakState> g_tweakState;
bool g_tweaksDirty = true;
bool g_restartNeeded = false;
std::string g_iniMessage;
std::vector<std::string> g_backups;

void RefreshTweaks() {
    g_tweakState.clear();
    for (const ini::Tweak& t : ini::Tweaks()) g_tweakState[t.id] = ini::StateOf(t);
    g_backups = ini::Backups();
    g_tweaksDirty = false;
}

void ContentTab() {
    if (!g_modsScanned) {
        g_mods = mods::ScanMods();
        g_modsScanned = true;
    }
    if (ImGui::Button("Rescan")) g_mods = mods::ScanMods();
    ImGui::SameLine();
    if (ImGui::Button("Open mods folder")) mods::OpenFolder(paths::ModSubdir("mods"));
    Hint("Put each fan-made mod (custom map, story DLC, texture or model package) in its own folder inside "
         "OutlastModMenu\\mods. The loader copies its files into the game, keeps backups of anything it replaces "
         "and can remove it again. See docs\\MODDING.md for the folder layout.");
    if (!g_modMessage.empty()) ImGui::TextWrapped("%s", g_modMessage.c_str());
    if (g_mods.empty()) ImGui::TextDisabled("No mods found.");
    for (size_t i = 0; i < g_mods.size(); ++i) {
        const mods::ContentMod& m = g_mods[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SeparatorText(m.name.c_str());
        ImGui::TextDisabled("%s%s%s  |  %d file(s)  |  %s", m.author.empty() ? "" : "by ", m.author.c_str(),
                            m.version.empty() ? "" : (" v" + m.version).c_str(), m.fileCount,
                            m.installed ? "installed" : "not installed");
        if (!m.description.empty()) ImGui::TextWrapped("%s", m.description.c_str());
        if (!m.installed) {
            if (ImGui::Button("Install")) {
                std::string err;
                g_modMessage = mods::Install(m, err) ? "Installed " + m.name + ". Restart the game if it was running "
                                                                                  "the files' level."
                                                     : "Install failed: " + err;
                g_modsScanned = false;
                g_mapsScanned = false;
            }
        } else {
            if (ImGui::Button("Uninstall")) {
                std::string err;
                g_modMessage = mods::Uninstall(m, err) ? "Removed " + m.name : "Uninstall failed: " + err;
                g_modsScanned = false;
                g_mapsScanned = false;
            }
            if (!m.startCheckpoint.empty() || !m.startMap.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Play")) {
                    std::string cp = m.startCheckpoint, map = m.startMap;
                    Enqueue([cp, map] {
                        if (!cp.empty()) actions::LoadCheckpoint(cp);
                        else mods::OpenMap(map);
                    });
                    OpenMenu(false);
                }
            }
        }
        ImGui::PopID();
    }
}

void MapsTab() {
    if (!g_mapsScanned) {
        g_maps = mods::ListMaps();
        g_mapsScanned = true;
    }
    if (ImGui::Button("Refresh")) g_maps = mods::ListMaps();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(Em(14));
    ImGui::InputTextWithHint("##mf", "filter", g_mapFilter, sizeof(g_mapFilter));
    Hint("Custom maps installed by mods are listed first. Opening one of the game's own story maps directly "
         "skips its scripting - use Teleport > Chapters for those.");
    ImGui::BeginChild("##maps", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const mods::MapFile& m : g_maps) {
        if (g_mapFilter[0] && !str::IContains(m.name, g_mapFilter)) continue;
        ImGui::PushID(m.relPath.c_str());
        if (ImGui::SmallButton("Open")) {
            std::string name = m.name;
            Enqueue([name] { mods::OpenMap(name); });
        }
        ImGui::SameLine();
        ImGui::Text("%s%s", m.name.c_str(), m.fromMod ? "  (mod)" : "");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m.relPath.c_str());
        ImGui::PopID();
    }
    if (g_maps.empty()) ImGui::TextDisabled("No map files found in the cooked content folder.");
    ImGui::EndChild();
}

void ReShadeTab() {
    if (!g_reshadeScanned) {
        g_reshade = mods::DetectReShade();
        g_reshadeScanned = true;
    }
    if (ImGui::Button("Detect again")) g_reshade = mods::DetectReShade();
    ImGui::SameLine();
    if (ImGui::Button("Open game folder")) mods::OpenFolder(paths::BinariesDir());
    if (g_reshade.installed) {
        Status(true, str::Format("ReShade found (%s)", g_reshade.dll.c_str()).c_str(), "");
        ImGui::TextDisabled("Active preset: %s", g_reshade.activePreset.empty() ? "(none)" : g_reshade.activePreset.c_str());
    } else {
        Status(false, "", "ReShade is not installed for Outlast.");
        Hint("Install ReShade from reshade.me and pick Binaries\\Win64\\OLGame.exe (Win32 for the 32-bit game) "
             "with Direct3D 9, the API Outlast renders with. The mod's overlay works together with ReShade.");
    }
    if (ImGui::Button("Install the mod's ReShade presets")) {
        std::string err;
        int n = mods::InstallBundledReShadePresets(err);
        g_modMessage = err.empty() ? str::Format("Copied %d preset(s) to reshade-presets.", n) : "Error: " + err;
        g_reshade = mods::DetectReShade();
    }
    Help("Copies the presets from OutlastModMenu\\reshade (e.g. a clarity preset and a brighter-nights preset). "
         "They use ReShade's standard effects.");
    if (!g_modMessage.empty()) ImGui::TextWrapped("%s", g_modMessage.c_str());
    ImGui::SeparatorText("Presets");
    if (g_reshade.presets.empty()) ImGui::TextDisabled("No presets found next to the game.");
    for (const mods::ReShadePreset& p : g_reshade.presets) {
        ImGui::PushID(p.path.c_str());
        if (ImGui::SmallButton("Use")) {
            std::string err;
            g_modMessage = mods::SetReShadePreset(g_reshade, p.path, err)
                               ? "Preset set - restart the game (or reload in ReShade's menu) to switch."
                               : "Error: " + err;
            g_reshade = mods::DetectReShade();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::PopID();
    }
}

void PluginsTab() {
    if (ImGui::Button("Open plugins folder")) mods::OpenFolder(paths::ModSubdir("plugins"));
    Hint("Plugins are DLLs (or .asi files) in OutlastModMenu\\plugins, loaded when the game starts. Plugins that "
         "export OMM_PluginInit can add their own options here - see sdk\\omm_plugin_api.h.");
    std::vector<plugins::PluginInfo> list = plugins::List();
    if (list.empty()) ImGui::TextDisabled("No plugins loaded.");
    for (const plugins::PluginInfo& p : list) {
        ImGui::BulletText("%s", p.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s) - %s", p.file.c_str(), p.status.c_str());
    }
    plugins::DrawMenuSections();
}
}  // namespace

void PageMods(Ctx&) {
    if (ImGui::BeginTabBar("##modtabs")) {
        if (ImGui::BeginTabItem("Fan-made content")) {
            ContentTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Maps")) {
            MapsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ReShade")) {
            ReShadeTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Plugins")) {
            PluginsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void PageIniTweaks(Ctx&) {
    if (g_tweaksDirty) RefreshTweaks();
    ImGui::TextDisabled("Config folder: %s", paths::UserConfigDir().c_str());
    if (ImGui::Button("Open config folder")) mods::OpenFolder(paths::UserConfigDir());
    ImGui::SameLine();
    if (ImGui::Button("Re-read files")) g_tweaksDirty = true;
    if (g_restartNeeded)
        ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f), "Restart Outlast for the changes to take effect.");
    if (!g_iniMessage.empty()) ImGui::TextWrapped("%s", g_iniMessage.c_str());

    Heading("Presets");
    for (const ini::Preset& p : ini::Presets()) {
        if (ImGui::Button(p.name)) {
            std::string err;
            int n = ini::ApplyPreset(p, err);
            g_iniMessage = err.empty() ? str::Format("'%s': %d tweak(s) applied.", p.name, n) : "Error: " + err;
            g_restartNeeded = true;
            g_tweaksDirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.description);
        ImGui::SameLine();
    }
    ImGui::NewLine();

    const char* category = nullptr;
    for (const ini::Tweak& t : ini::Tweaks()) {
        if (!category || std::string(category) != t.category) {
            category = t.category;
            Heading(category);
        }
        ini::TweakState st = g_tweakState.count(t.id) ? g_tweakState[t.id] : ini::TweakState::Off;
        bool on = st == ini::TweakState::On;
        ImGui::PushID(t.id);
        if (st == ini::TweakState::Partial) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        if (ImGui::Checkbox(t.title, &on)) {
            std::string err;
            bool ok = on ? ini::Apply(t, err) : ini::Revert(t, err);
            g_iniMessage = ok ? std::string(on ? "Applied: " : "Reverted: ") + t.title : "Error: " + err;
            g_restartNeeded = true;
            g_tweaksDirty = true;
        }
        if (st == ini::TweakState::Partial) ImGui::PopItemFlag();
        Help(t.description);
        ImGui::PopID();
    }

    Heading("Backups");
    if (ImGui::Button("Back up now")) {
        std::string err;
        std::string dir = ini::BackupNow(err);
        g_iniMessage = dir.empty() ? "Backup failed: " + err : "Saved to " + dir;
        g_tweaksDirty = true;
    }
    Hint("'original' is the copy made before the mod changed anything.");
    for (const std::string& b : g_backups) {
        ImGui::PushID(b.c_str());
        ImGui::BulletText("%s", b.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Restore")) ImGui::OpenPopup("restore?");
        if (ImGui::BeginPopup("restore?")) {
            ImGui::Text("Replace your current config files with '%s'?", b.c_str());
            if (ImGui::Button("Restore")) {
                std::string err;
                g_iniMessage = ini::RestoreBackup(b, err) ? "Restored " + b + " - restart Outlast." : "Error: " + err;
                g_restartNeeded = true;
                g_tweaksDirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}

}  // namespace omm::ui
