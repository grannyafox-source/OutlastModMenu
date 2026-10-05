// One-shot actions used by the menu and the hotkeys. They all run on the
// game thread: the UI wraps them in Enqueue().
#pragma once

#include "../ue3/types.h"

#include <string>
#include <vector>

namespace omm::game::actions {

// --- Cameras / noclip (OLCheatManager) -----------------------------------------
void ToggleNoclip();                  // "Ghost": fly through walls, the player follows
void ToggleFreecam(bool pauseGame);   // detached camera; the player stays put
void ToggleFixedCam();
void TeleportToFreecam();             // move the player to the free camera
void ExitAllCameraModes();

// --- Player --------------------------------------------------------------------
void Heal();
void KillPlayer();
void AddBatteries(int count);
void RefillCurrentBattery();
void GiveCamcorder(bool give);
void TeleportToCrosshair();
void SetHeroScale(float scale);

enum class Outfit { Original = 0, Fingerless, ITTech, Prisoner };
const char* OutfitName(Outfit o);
bool SetOutfit(Outfit o);

// Applies a named character preset (limp, hobble, cracked lens...) to the
// UI state. Render thread.
struct CharacterPreset {
    const char* name;
    const char* description;
};
const std::vector<CharacterPreset>& CharacterPresets();
void ApplyCharacterPreset(int index);  // render thread (edits state::Ui())

// --- Teleport ------------------------------------------------------------------
struct SavedPosition {
    std::string key;    // config key
    std::string map;
    std::string label;
    ue3::FVector location;
    ue3::FRotator rotation;
};
std::vector<SavedPosition> SavedPositions(const std::string& map);  // any thread
void SavePosition(const std::string& label);                        // game thread
void DeletePosition(const std::string& key);                        // any thread
bool GoToPosition(const SavedPosition& p);                          // game thread
void QuickSavePosition();
void QuickLoadPosition();
bool TeleportRandomLocation();
bool LoadCheckpoint(const std::string& name);
std::string RandomCheckpoint(bool includeMain, bool includeDlc);  // any thread
std::string RandomScene(bool includeMain, bool includeDlc);       // any thread

// Checkpoint names found in the running game (OLCheckpointList), if any.
std::vector<std::string> GameCheckpointList();  // any thread
void RefreshGameCheckpointList();               // game thread

// --- Console -------------------------------------------------------------------
void RunConsoleCommand(const std::string& command);  // output goes to ConsoleLog()
std::vector<std::string> ConsoleLog();               // any thread

}  // namespace omm::game::actions
