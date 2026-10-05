// Player, Movement, Camera and Batteries pages.
#include "pages.h"

#include "../core/strutil.h"
#include "../game/actions.h"

namespace omm::ui {

using namespace game;

namespace {
void CameraModeButtons(const Snapshot& snap) {
    if (ImGui::Button(snap.ghost ? "Noclip: ON" : "Noclip: OFF")) Enqueue([] { actions::ToggleNoclip(); });
    Help("Fly through walls (the game's own 'Ghost' cheat). Move with your normal keys; look with the mouse.");
    ImGui::SameLine();
    if (ImGui::Button(snap.freeCam ? "Free camera: ON" : "Free camera: OFF")) Enqueue([] { actions::ToggleFreecam(false); });
    Help("Detach the camera and fly it around while the game keeps running.");
    ImGui::SameLine();
    if (ImGui::Button("Free camera (paused)")) Enqueue([] { actions::ToggleFreecam(true); });
    Help("Same, but the game is paused - perfect for screenshots.");
    if (ImGui::Button("Teleport player to camera")) Enqueue([] { actions::TeleportToFreecam(); });
    ImGui::SameLine();
    if (ImGui::Button(snap.fixedCam ? "Fixed camera: ON" : "Fixed camera: OFF")) Enqueue([] { actions::ToggleFixedCam(); });
    Help("Freezes the camera in place while you keep controlling Miles - a third-person view.");
    ImGui::SameLine();
    if (ImGui::Button("Back to normal view")) Enqueue([] { actions::ExitAllCameraModes(); });
}
}  // namespace

void PagePlayer(Ctx& c) {
    const Snapshot& snap = c.snap;
    ModState& s = c.s;
    if (snap.inGame) {
        float hp = snap.healthMax > 0 ? static_cast<float>(snap.health) / static_cast<float>(snap.healthMax) : 0.f;
        ImGui::ProgressBar(Clamp(hp, 0.f, 1.f), ImVec2(Em(16), 0), str::Format("Health %d / %d", snap.health, snap.healthMax).c_str());
        ImGui::SameLine();
        ImGui::Text("Batteries: %d / %d", snap.batteries, snap.maxBatteries);
        ImGui::TextDisabled("Position %.0f, %.0f, %.0f   Speed %.0f   Documents %d   Recordings %d", snap.location.X,
                            snap.location.Y, snap.location.Z, snap.speed, snap.documents, snap.recordings);
    } else {
        NeedsGame(snap);
    }

    Heading("Survival");
    Toggle("God mode", &s.godMode, "Enemies, falls and hazards can't kill you.");
    Toggle("Infinite health", &s.infiniteHealth, "Health is refilled every frame (works even where god mode is ignored).");
    Toggle("Invisible to enemies", &s.invisible,
           "Enemies don't see, hear or chase you (the game's own 'ghost' flag on the player, plus blind and deaf "
           "enemies).");
    Toggle("Silent footsteps", &s.silentFootsteps, "Running, landing, doors and lockers make no noise enemies can hear.");
    Toggle("No fall damage", &s.noFallDamage);
    Toggle("Fast health regeneration", &s.fastHealthRegen);
    if (s.fastHealthRegen) {
        ImGui::Indent();
        SliderF("Delay before regen (s)", &s.regenDelay, 0.f, 10.f, "%.1f", 10.f);
        SliderF("Regen per second", &s.regenRate, 1.f, 100.f, "%.0f", 10.f);
        ImGui::Unindent();
    }
    Toggle("Custom max health", &s.overrideMaxHealth);
    if (s.overrideMaxHealth) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(Em(10));
        ImGui::SliderInt("##maxhp", &s.maxHealth, 1, 1000);
    }

    Heading("Quick actions");
    if (ImGui::Button("Heal")) Enqueue([] { actions::Heal(); });
    ImGui::SameLine();
    if (ImGui::Button("Teleport to crosshair")) Enqueue([] { actions::TeleportToCrosshair(); });
    ImGui::SameLine();
    if (ImGui::Button("Save position")) Enqueue([] { actions::QuickSavePosition(); });
    ImGui::SameLine();
    if (ImGui::Button("Load position")) Enqueue([] { actions::QuickLoadPosition(); });
    ImGui::SameLine();
    if (DangerButton("Kill player")) ImGui::OpenPopup("Kill?");
    if (ImGui::BeginPopup("Kill?")) {
        ImGui::TextUnformatted("Really die? The game reloads the last checkpoint.");
        if (ImGui::Button("Yes")) {
            Enqueue([] { actions::KillPlayer(); });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    Heading("Noclip / free camera");
    CameraModeButtons(snap);
    SliderF("Fly speed", &s.noclipSpeed, 0.f, 5000.f, s.noclipSpeed <= 0.f ? "game default" : "%.0f", 0.f,
            "Speed of noclip and the free camera. 0 = the game's default.");
}

void PageMovement(Ctx& c) {
    ModState& s = c.s;
    Heading("Movement speed");
    Toggle("Change movement speed", &s.speedOverride);
    ImGui::BeginDisabled(!s.speedOverride);
    SliderF("Speed multiplier", &s.speedMultiplier, 0.1f, 10.f, "%.2fx", 1.f, "Multiplies every speed below.");
    ImGui::TextUnformatted("Presets:");
    ImGui::SameLine();
    const struct { const char* name; float mul; } presets[] = {{"Slow 0.5x", 0.5f}, {"Normal", 1.f}, {"Fast 1.5x", 1.5f},
                                                               {"Sprinter 2x", 2.f}, {"Sonic 5x", 5.f}};
    for (const auto& p : presets) {
        if (ImGui::SmallButton(p.name)) s.speedMultiplier = p.mul;
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (ImGui::TreeNode("Individual speeds (units per second)")) {
        SliderF("Walk", &s.walkSpeed, 50.f, 2000.f, "%.0f", 200.f);
        SliderF("Run", &s.runSpeed, 50.f, 3000.f, "%.0f", 450.f);
        SliderF("Crouch", &s.crouchSpeed, 20.f, 1000.f, "%.0f", 75.f);
        SliderF("Walk in water", &s.waterWalkSpeed, 20.f, 1000.f, "%.0f", 100.f);
        SliderF("Run in water", &s.waterRunSpeed, 20.f, 2000.f, "%.0f", 200.f);
        SliderF("Limping", &s.limpWalkSpeed, 20.f, 1000.f, "%.0f", 87.243f);
        SliderF("Hobbling walk", &s.hobbleWalkSpeed, 20.f, 1000.f, "%.0f", 140.f);
        SliderF("Hobbling run", &s.hobbleRunSpeed, 20.f, 2000.f, "%.0f", 250.f);
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
    Toggle("Full speed backwards and sideways", &s.noDirectionPenalty,
           "Outlast normally slows you down 35% walking backwards and 20% strafing.");

    Heading("Jumping and gravity");
    Toggle("Change jump height", &s.jumpOverride);
    if (s.jumpOverride) SliderF("Jump multiplier", &s.jumpMultiplier, 0.2f, 6.f, "%.2fx", 1.f);
    Toggle("Change gravity", &s.gravityOverride, "Low gravity makes jumps float; affects everything in the level.");
    if (s.gravityOverride) SliderF("Gravity multiplier", &s.gravityMultiplier, 0.05f, 3.f, "%.2fx", 1.f);
    Hint("Tip: speed changes stack with the World page's game speed. Very high speeds can push you through thin walls.");
}

void PageCamera(Ctx& c) {
    ModState& s = c.s;
    Heading("Field of view");
    Toggle("Custom field of view", &s.fovOverride, "Outlast uses 90 degrees walking and 100 while running.");
    ImGui::BeginDisabled(!s.fovOverride);
    SliderF("Walking FOV", &s.fov, 60.f, 130.f, "%.0f", 90.f);
    SliderF("Running FOV", &s.runFov, 60.f, 140.f, "%.0f", 100.f);
    ImGui::EndDisabled();
    Toggle("Custom camcorder zoom range", &s.camcorderZoomOverride, "Widest camcorder angle (game default 83).");
    if (s.camcorderZoomOverride) SliderF("Camcorder widest FOV", &s.camcorderMaxFov, 40.f, 120.f, "%.0f", 83.f);

    Heading("Free camera");
    CameraModeButtons(c.snap);
    SliderF("Camera speed", &s.noclipSpeed, 0.f, 5000.f, s.noclipSpeed <= 0.f ? "game default" : "%.0f", 0.f);
    Hint("The free camera keeps the game running unless you choose the paused version. Use 'Teleport player to "
         "camera' to move Miles where the camera is.");
}

void PageBatteries(Ctx& c) {
    const Snapshot& snap = c.snap;
    ModState& s = c.s;
    if (snap.inGame) {
        ImGui::ProgressBar(Clamp(snap.batteryEnergy, 0.f, 1.f), ImVec2(Em(16), 0),
                           str::Format("Current battery %.0f%%", snap.batteryEnergy * 100.f).c_str());
        ImGui::SameLine();
        ImGui::Text("Spare batteries: %d / %d", snap.batteries, snap.maxBatteries);
    }
    Heading("Batteries");
    Toggle("Unlimited batteries", &s.unlimitedBatteries, "Night vision never drains (the game's own cheat flag).");
    Toggle("Lock battery count", &s.lockBatteryCount);
    if (s.lockBatteryCount) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(Em(8));
        ImGui::SliderInt("##batcount", &s.batteryCount, 0, 99);
    }
    Toggle("Custom battery capacity", &s.overrideMaxBatteries, "How many spare batteries you can carry (Normal: 10).");
    if (s.overrideMaxBatteries) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(Em(8));
        ImGui::SliderInt("##batmax", &s.maxBatteries, 1, 99);
    }
    Toggle("Custom battery duration", &s.overrideBatteryDuration);
    if (s.overrideBatteryDuration) SliderF("Seconds per battery", &s.batteryDuration, 10.f, 3600.f, "%.0f s", 150.f);

    if (ImGui::Button("+1 battery")) Enqueue([] { actions::AddBatteries(1); });
    ImGui::SameLine();
    if (ImGui::Button("+5 batteries")) Enqueue([] { actions::AddBatteries(5); });
    ImGui::SameLine();
    if (ImGui::Button("-1 battery")) Enqueue([] { actions::AddBatteries(-1); });
    ImGui::SameLine();
    if (ImGui::Button("Refill current battery")) Enqueue([] { actions::RefillCurrentBattery(); });

    Heading("Night vision");
    Toggle("Stronger night vision", &s.nightVisionBoost, "Longer and brighter night-vision light.");
    if (s.nightVisionBoost) {
        SliderF("Range", &s.nightVisionRange, 500.f, 8000.f, "%.0f", 1500.f);
        SliderF("Brightness", &s.nightVisionBrightness, 0.005f, 0.3f, "%.3f", 0.025f);
    }

    Heading("Camcorder");
    if (ImGui::Button(snap.hasCamcorder ? "Remove camcorder" : "Give camcorder")) {
        bool give = !snap.hasCamcorder;
        Enqueue([give] { actions::GiveCamcorder(give); });
    }
    Help("Only for fun: some story scenes expect you to have (or not have) the camcorder.");
}

}  // namespace omm::ui
