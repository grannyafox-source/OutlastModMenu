// Locations the mod needs: its own folder, the game install and the folder
// holding the user's OL*.ini files.
#pragma once

#include <string>

namespace omm::paths {

// Must be called once with the module handle of the mod DLL.
void Init(void* modDllModule);

const std::string& ModDllPath();     // full path of our DLL
const std::string& ModDir();         // <dll dir>\OutlastModMenu
const std::string& GameExePath();    // ...\Binaries\Win64\OLGame.exe
const std::string& BinariesDir();    // folder of OLGame.exe
const std::string& GameRoot();       // install root (contains OLGame\ and Binaries\)
const std::string& UserConfigDir();  // folder with OLEngine.ini / OLGame.ini / ...
const std::string& CookedDir();      // <root>\OLGame\CookedPCConsole (or CookedPC)

// Unit tests: point every location at a temporary folder.
void SetForTests(const std::string& modDir, const std::string& gameRoot, const std::string& configDir);

std::string ModSubdir(const char* name);  // creates it if missing
std::string ConfigFile(const char* fileName);  // UserConfigDir()\fileName

}  // namespace omm::paths
