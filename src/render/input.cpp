#include "input.h"

#include "../core/log.h"
#include "../core/sync.h"

#if OMM_WINDOWS

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <vector>

#include "MinHook.h"
#include "imgui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace omm::input {

namespace {
HWND g_hwnd = nullptr;
WNDPROC g_origProc = nullptr;  // the procedure we chain to (the game's own)
bool g_reportedForeign = false;
volatile LONG g_depth = 0;     // re-entrancy of HookedWndProc (window thread only)
bool g_reportedLoop = false;
HotkeyHandler g_hotkeys = nullptr;
volatile LONG g_menuOpen = 0;
volatile LONG g_overlayWantsMouse = 0;

struct QueuedMsg {
    HWND hwnd;
    UINT msg;
    WPARAM wp;
    LPARAM lp;
};
Mutex g_queueLock;
std::vector<QueuedMsg> g_queue;

bool IsMouseMsg(UINT m) {
    return (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || m == WM_MOUSEHWHEEL;
}
bool IsKeyMsg(UINT m) {
    return m == WM_KEYDOWN || m == WM_KEYUP || m == WM_SYSKEYDOWN || m == WM_SYSKEYUP || m == WM_CHAR ||
           m == WM_SYSCHAR || m == WM_DEADCHAR;
}

void Queue(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    LockGuard lock(g_queueLock);
    if (g_queue.size() < 4096) g_queue.push_back({h, m, wp, lp});
}

LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Messages can legitimately nest a few levels (SendMessage from inside a
    // handler). Very deep nesting means two subclassers keep calling each
    // other; break the loop by going straight to the game's procedure.
    if (g_depth > 48) {
        if (!g_reportedLoop) {
            g_reportedLoop = true;
            LOGE("Window procedure loop detected - bypassing the mod's input hook for this message");
        }
        return g_origProc ? CallWindowProcW(g_origProc, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
    }
    ++g_depth;
    LRESULT r = HandleMessage(hwnd, msg, wp, lp);
    --g_depth;
    return r;
}

LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        bool repeat = down && (lp & (1 << 30));
        if (g_hotkeys && g_hotkeys(static_cast<int>(wp), down, repeat)) {
            Queue(hwnd, msg, wp, lp);  // keep ImGui's key state consistent
            return 0;
        }
    }

    bool menu = g_menuOpen != 0;
    if (IsMouseMsg(msg) || IsKeyMsg(msg) || msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_ACTIVATEAPP ||
        msg == WM_MOUSELEAVE) {
        Queue(hwnd, msg, wp, lp);
    }

    if (menu) {
        // Let Alt+F4 and Alt+Tab work; everything else belongs to the menu.
        if (msg == WM_SYSKEYDOWN && (wp == VK_F4 || wp == VK_TAB)) return CallWindowProcW(g_origProc, hwnd, msg, wp, lp);
        if (IsMouseMsg(msg) || IsKeyMsg(msg)) return 0;
    } else if (g_overlayWantsMouse && IsMouseMsg(msg) && msg != WM_MOUSEMOVE) {
        // The cursor is over one of our buttons drawn on top of the game's
        // menu: don't let the click fall through to the game.
        return 0;
    }
    return CallWindowProcW(g_origProc, hwnd, msg, wp, lp);
}

// --- DirectInput ---------------------------------------------------------------
using GetDeviceStateFn = HRESULT(STDMETHODCALLTYPE*)(void* self, DWORD cb, LPVOID data);
using GetDeviceDataFn = HRESULT(STDMETHODCALLTYPE*)(void* self, DWORD cbObj, LPDIDEVICEOBJECTDATA data, LPDWORD inOut,
                                                    DWORD flags);
GetDeviceStateFn g_origStateW = nullptr, g_origStateA = nullptr;
GetDeviceDataFn g_origDataW = nullptr, g_origDataA = nullptr;
void* g_targetStateW = nullptr;  // hooked function addresses (not trampolines)
void* g_targetDataW = nullptr;

struct DevType {
    void* dev;
    bool mouse;
};
DevType g_devTypes[32];
int g_devCount = 0;
Mutex g_devLock;

bool IsMouseDevice(void* self, bool wide) {
    {
        LockGuard lock(g_devLock);
        for (int i = 0; i < g_devCount; ++i)
            if (g_devTypes[i].dev == self) return g_devTypes[i].mouse;
    }
    DIDEVCAPS caps{};
    caps.dwSize = sizeof(caps);
    HRESULT hr = wide ? static_cast<IDirectInputDevice8W*>(self)->GetCapabilities(&caps)
                      : static_cast<IDirectInputDevice8A*>(self)->GetCapabilities(&caps);
    bool mouse = SUCCEEDED(hr) && GET_DIDEVICE_TYPE(caps.dwDevType) == DI8DEVTYPE_MOUSE;
    LockGuard lock(g_devLock);
    if (g_devCount < 32) g_devTypes[g_devCount++] = {self, mouse};
    return mouse;
}

bool BlockMouse() { return g_menuOpen != 0; }

HRESULT STDMETHODCALLTYPE HkGetDeviceStateW(void* self, DWORD cb, LPVOID data) {
    HRESULT hr = g_origStateW(self, cb, data);
    if (SUCCEEDED(hr) && data && BlockMouse() && IsMouseDevice(self, true)) std::memset(data, 0, cb);
    return hr;
}
HRESULT STDMETHODCALLTYPE HkGetDeviceStateA(void* self, DWORD cb, LPVOID data) {
    HRESULT hr = g_origStateA(self, cb, data);
    if (SUCCEEDED(hr) && data && BlockMouse() && IsMouseDevice(self, false)) std::memset(data, 0, cb);
    return hr;
}
HRESULT STDMETHODCALLTYPE HkGetDeviceDataW(void* self, DWORD cbObj, LPDIDEVICEOBJECTDATA data, LPDWORD inOut,
                                          DWORD flags) {
    HRESULT hr = g_origDataW(self, cbObj, data, inOut, flags);
    if (SUCCEEDED(hr) && inOut && BlockMouse() && IsMouseDevice(self, true)) *inOut = 0;  // events consumed
    return hr;
}
HRESULT STDMETHODCALLTYPE HkGetDeviceDataA(void* self, DWORD cbObj, LPDIDEVICEOBJECTDATA data, LPDWORD inOut,
                                          DWORD flags) {
    HRESULT hr = g_origDataA(self, cbObj, data, inOut, flags);
    if (SUCCEEDED(hr) && inOut && BlockMouse() && IsMouseDevice(self, false)) *inOut = 0;
    return hr;
}

template <typename Fn>
bool Detour(void* target, void* detour, Fn* original) {
    if (!target) return false;
    MH_STATUS st = MH_CreateHook(target, detour, reinterpret_cast<void**>(original));
    if (st != MH_OK && st != MH_ERROR_ALREADY_CREATED) return false;
    st = MH_EnableHook(target);
    return st == MH_OK || st == MH_ERROR_ENABLED;
}
}  // namespace

void SetHotkeyHandler(HotkeyHandler h) { g_hotkeys = h; }

bool InstallWndProc(HWND hwnd) {
    if (!hwnd) return false;
    if (g_hwnd == hwnd && g_origProc) return true;
    WNDPROC prev = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));
    if (!prev) {
        LOGE("SetWindowLongPtr failed (%lu)", GetLastError());
        return false;
    }
    g_origProc = prev;
    g_hwnd = hwnd;
    LOGI("Game window %p subclassed", static_cast<void*>(hwnd));
    return true;
}

void CheckWndProc() {
    if (!g_hwnd || !IsWindow(g_hwnd)) return;
    LONG_PTR cur = GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC);
    if (cur == reinterpret_cast<LONG_PTR>(&HookedWndProc)) return;
    // Another component subclassed the window after the mod (the game's
    // Scaleform IME support, overlays...). It keeps calling our procedure as
    // its "previous" one, so it must NOT be wrapped again: that would make the
    // two procedures call each other forever. Only when the game put its own
    // procedure back (dropping ours) is it safe to install ours again.
    if (cur == reinterpret_cast<LONG_PTR>(g_origProc)) {
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc));
        LOGW("The game restored its window procedure - input hook re-installed");
    } else if (!g_reportedForeign) {
        g_reportedForeign = true;
        LOGI("Another component subclassed the game window after the mod; leaving it in place");
    }
}

HWND Window() { return g_hwnd; }

bool InstallDirectInputHooks(HMODULE dinput8) {
    if (!dinput8) return false;
    using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    auto create = reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(dinput8, "DirectInput8Create")));
    if (!create) return false;
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return false;
    bool any = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);

    IDirectInput8W* diW = nullptr;
    if (SUCCEEDED(create(inst, DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&diW), nullptr))) {
        IDirectInputDevice8W* dev = nullptr;
        if (SUCCEEDED(diW->CreateDevice(GUID_SysMouse, &dev, nullptr))) {
            void** vt = *reinterpret_cast<void***>(dev);
            if (Detour(vt[9], reinterpret_cast<void*>(&HkGetDeviceStateW), &g_origStateW)) {
                g_targetStateW = vt[9];
                any = true;
            }
            if (Detour(vt[10], reinterpret_cast<void*>(&HkGetDeviceDataW), &g_origDataW)) {
                g_targetDataW = vt[10];
                any = true;
            }
            dev->Release();
        }
        diW->Release();
    }
    IDirectInput8A* diA = nullptr;
    if (SUCCEEDED(create(inst, DIRECTINPUT_VERSION, IID_IDirectInput8A, reinterpret_cast<void**>(&diA), nullptr))) {
        IDirectInputDevice8A* dev = nullptr;
        if (SUCCEEDED(diA->CreateDevice(GUID_SysMouse, &dev, nullptr))) {
            void** vt = *reinterpret_cast<void***>(dev);
            // The A and W entry points can be the same function; only hook once.
            if (vt[9] != g_targetStateW)
                any |= Detour(vt[9], reinterpret_cast<void*>(&HkGetDeviceStateA), &g_origStateA);
            if (vt[10] != g_targetDataW)
                any |= Detour(vt[10], reinterpret_cast<void*>(&HkGetDeviceDataA), &g_origDataA);
            dev->Release();
        }
        diA->Release();
    }
    LOGI("DirectInput mouse hooks %s", any ? "installed" : "NOT installed");
    return any;
}

void Uninstall() {
    if (g_hwnd && g_origProc && IsWindow(g_hwnd))
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origProc));
    g_hwnd = nullptr;
}

void SetMenuOpen(bool open) { InterlockedExchange(&g_menuOpen, open ? 1 : 0); }
bool MenuOpen() { return g_menuOpen != 0; }
void SetOverlayWantsMouse(bool wants) { InterlockedExchange(&g_overlayWantsMouse, wants ? 1 : 0); }

void PumpToImGui() {
    std::vector<QueuedMsg> msgs;
    {
        LockGuard lock(g_queueLock);
        msgs.swap(g_queue);
    }
    for (const QueuedMsg& m : msgs) ImGui_ImplWin32_WndProcHandler(m.hwnd, m.msg, m.wp, m.lp);
}

}  // namespace omm::input

#else  // !OMM_WINDOWS

namespace omm::input {
void SetHotkeyHandler(HotkeyHandler) {}
void Uninstall() {}
void SetMenuOpen(bool) {}
bool MenuOpen() { return false; }
void SetOverlayWantsMouse(bool) {}
void PumpToImGui() {}
}  // namespace omm::input

#endif
