// ImGui overlay drawn on top of the game. Outlast renders with Direct3D 9;
// Direct3D 11 is hooked as well for wrappers that translate Direct3D 9 to 11
// (dgVoodoo and similar) and present through DXGI. Runs on the game's render
// thread.
#pragma once

namespace omm::render {

enum class Backend { None, D3D9, D3D11 };

using SetupCallback = void (*)();  // once, right after the ImGui context exists
using FrameCallback = void (*)();  // every frame, between NewFrame and Render

void SetCallbacks(SetupCallback setup, FrameCallback frame);

// Installs Present/Reset hooks. Safe to call before the game creates its
// device; returns false only if neither API could be hooked.
bool InstallHooks();
void Shutdown();

Backend ActiveBackend();
const char* BackendName();
bool ImGuiReady();
unsigned long long FrameCount();
float FrameRate();  // smoothed presents per second

}  // namespace omm::render
