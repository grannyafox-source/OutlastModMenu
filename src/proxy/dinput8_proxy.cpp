// dinput8.dll proxy.
//
// Unreal Engine 3 creates its mouse device with DirectInput8Create, so a
// dinput8.dll placed next to OLGame.exe is loaded by Windows at startup.
// Every export forwards to the real DLL: first "dinput8_chain.dll" next to
// us (lets another dinput8-based mod keep working), otherwise the system copy.
#include "../core/common.h"

#if OMM_WINDOWS

#include <objbase.h>

namespace {
HMODULE g_real = nullptr;
HMODULE g_self = nullptr;

HMODULE LoadReal() {
    if (g_real) return g_real;
    wchar_t path[MAX_PATH * 2] = {};
    if (g_self && GetModuleFileNameW(g_self, path, MAX_PATH)) {
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash) {
            wcscpy(slash + 1, L"dinput8_chain.dll");
            if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) g_real = LoadLibraryW(path);
        }
    }
    if (!g_real) {
        UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (n && n < MAX_PATH - 20) {
            wcscat(path, L"\\dinput8.dll");
            g_real = LoadLibraryW(path);
        }
    }
    return g_real;
}

template <typename T>
T Real(const char* name) {
    HMODULE m = LoadReal();
    return m ? reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(m, name))) : nullptr;
}
}  // namespace

namespace omm::proxy {
void SetSelf(HMODULE self) { g_self = self; }
HMODULE RealDInput8() { return LoadReal(); }
}  // namespace omm::proxy

extern "C" {

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE hinst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    using Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    Fn fn = Real<Fn>("DirectInput8Create");
    if (!fn) return E_FAIL;
    return fn(hinst, version, riid, out, outer);
}

HRESULT WINAPI Proxy_DllCanUnloadNow() {
    using Fn = HRESULT(WINAPI*)();
    Fn fn = Real<Fn>("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}

HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* out) {
    using Fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    Fn fn = Real<Fn>("DllGetClassObject");
    return fn ? fn(clsid, riid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI Proxy_DllRegisterServer() {
    using Fn = HRESULT(WINAPI*)();
    Fn fn = Real<Fn>("DllRegisterServer");
    return fn ? fn() : E_FAIL;
}

HRESULT WINAPI Proxy_DllUnregisterServer() {
    using Fn = HRESULT(WINAPI*)();
    Fn fn = Real<Fn>("DllUnregisterServer");
    return fn ? fn() : E_FAIL;
}

const void* WINAPI Proxy_GetdfDIJoystick() {
    using Fn = const void*(WINAPI*)();
    Fn fn = Real<Fn>("GetdfDIJoystick");
    return fn ? fn() : nullptr;
}

}  // extern "C"

#endif  // OMM_WINDOWS
