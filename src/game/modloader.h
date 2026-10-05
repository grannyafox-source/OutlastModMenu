// Built-in mod loader: fan-made content (custom maps / "DLC" packages),
// ReShade presets and plugins (see plugins.h).
//
// Content mods live in "OutlastModMenu\mods\<ModName>":
//
//   mod.ini            [Mod] Name, Author, Version, Description,
//                      StartMap (opened with "open <map>"), StartCheckpoint
//   CookedPCConsole   folder, copied to <Outlast>\OLGame\CookedPCConsole\OMM_Mods\<ModName>
//   Game              folder, copied over the install folder keeping sub-folders
//                      (originals are backed up and restored on uninstall)
#pragma once

#include <string>
#include <vector>

namespace omm::game::mods {

struct ContentMod {
    std::string folder;  // name of the folder in OutlastModMenu\mods
    std::string name;
    std::string author;
    std::string version;
    std::string description;
    std::string startMap;
    std::string startCheckpoint;
    int fileCount = 0;
    bool installed = false;
};

std::vector<ContentMod> ScanMods();  // file I/O, call from the UI on demand
bool Install(const ContentMod& m, std::string& error);
bool Uninstall(const ContentMod& m, std::string& error);

struct MapFile {
    std::string name;  // what "open" expects
    std::string relPath;
    bool fromMod = false;
};
std::vector<MapFile> ListMaps();
bool OpenMap(const std::string& map);  // game thread

// ReShade -----------------------------------------------------------------------
struct ReShadePreset {
    std::string name;
    std::string path;
};
struct ReShadeInfo {
    bool installed = false;
    std::string dll;         // e.g. dxgi.dll
    std::string configPath;  // ReShade.ini
    std::string activePreset;
    std::vector<ReShadePreset> presets;
};
ReShadeInfo DetectReShade();
bool SetReShadePreset(const ReShadeInfo& info, const std::string& presetPath, std::string& error);
// Copies the presets shipped in OutlastModMenu\reshade next to the game.
int InstallBundledReShadePresets(std::string& error);

void OpenFolder(const std::string& path);  // Explorer

}  // namespace omm::game::mods
