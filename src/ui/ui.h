// The in-game menu (Dear ImGui). Runs on the render thread; talks to the
// game thread only through game::state (ModState / Snapshot / Enqueue).
#pragma once

namespace omm::ui {

void Setup();  // once, after the ImGui context exists
void Frame();  // every frame, between ImGui::NewFrame and ImGui::Render

// Window thread: returns true when the key was a mod hotkey (swallowed).
bool OnHotkey(int vk, bool down, bool repeat);

void OpenMenu(bool open);
bool IsMenuOpen();

}  // namespace omm::ui
