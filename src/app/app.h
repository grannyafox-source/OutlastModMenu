// Start-up sequence of the mod (runs on its own thread, never in DllMain).
#pragma once

#include "../core/common.h"

namespace omm::app {

#if OMM_WINDOWS
// True when the DLL was loaded into Outlast (OLGame.exe) and no other copy
// of the mod is running in this process. Safe to call from DllMain.
bool ShouldStart(HMODULE self);
DWORD WINAPI InitThread(LPVOID selfModule);
#endif

}  // namespace omm::app
