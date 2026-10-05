// Input plumbing between the game window, the game and the overlay.
//
// * The game window is subclassed; its messages are queued for ImGui (which
//   runs on the render thread) and swallowed while the mod menu is open.
// * Unreal Engine 3 reads the mouse through DirectInput, so mouse movement
//   is blocked at IDirectInputDevice8::GetDeviceData/GetDeviceState too.
#pragma once

#include "../core/common.h"

namespace omm::input {

// Hotkey handler, called on the window thread. Return true to swallow the key.
using HotkeyHandler = bool (*)(int vk, bool down, bool repeat);

void SetHotkeyHandler(HotkeyHandler h);

#if OMM_WINDOWS
bool InstallWndProc(HWND hwnd);
void CheckWndProc();  // re-installs if the game replaced it
HWND Window();
bool InstallDirectInputHooks(HMODULE dinput8);
#endif
void Uninstall();

// The overlay decides (on the render thread) whether it wants the input.
void SetMenuOpen(bool open);
bool MenuOpen();
void SetOverlayWantsMouse(bool wants);  // e.g. cursor over the "MOD MENU" button

// Replays queued window messages into ImGui. Render thread only.
void PumpToImGui();

}  // namespace omm::input
