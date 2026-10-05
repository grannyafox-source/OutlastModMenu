// DLL entry point. The same binary works as:
//   * dinput8.dll next to OLGame.exe (loaded by the game itself),
//   * an .asi plugin for an ASI loader,
//   * a DLL injected with tools/injector.
#include "app.h"

#if OMM_WINDOWS

namespace omm::proxy {
void SetSelf(HMODULE self);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        omm::proxy::SetSelf(instance);
        // Only kernel32 work is allowed under the loader lock: everything else
        // happens on the init thread, which starts once loading has finished.
        if (omm::app::ShouldStart(instance)) {
            HANDLE thread = CreateThread(nullptr, 0, &omm::app::InitThread, instance, 0, nullptr);
            if (thread) CloseHandle(thread);
        }
    }
    return TRUE;
}

#endif
