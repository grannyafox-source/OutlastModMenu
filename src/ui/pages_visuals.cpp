// Visuals, ESP and Performance pages.
#include "pages.h"

#include "../core/strutil.h"
#include "../game/frame.h"
#include "../game/initweaks.h"
#include "../game/performance.h"
#include "../render/overlay.h"

namespace omm::ui {

using namespace game;

void PageVisuals(Ctx& c) {
    ModState& s = c.s;
    Heading("Brightness");
    const struct {
        const char* name;
        float gamma, shadows, midtones, highlights;
    } presets[] = {{"Game default", 2.2f, 1.f, 1.f, 1.f},
                   {"Brighter", 2.6f, 1.35f, 1.1f, 1.f},
                   {"Very bright", 3.0f, 1.8f, 1.25f, 1.f},
                   {"See in the dark", 3.4f, 2.6f, 1.5f, 1.f}};
    for (const auto& p : presets) {
        if (ImGui::Button(p.name)) {
            bool def = p.gamma == 2.2f && p.shadows == 1.f;
            s.gammaOverride = !def;
            s.brightnessOverride = !def;
            s.gamma = p.gamma;
            s.shadowsMul = p.shadows;
            s.midtonesMul = p.midtones;
            s.highlightsMul = p.highlights;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    Toggle("Custom gamma", &s.gammaOverride, "The same setting as the game's brightness slider, but with a wider range.");
    if (s.gammaOverride) SliderF("Gamma", &s.gamma, 1.f, 5.f, "%.2f", 2.2f);
    Toggle("Brightness multiplier", &s.brightnessOverride, "Brightens dark areas without washing out the highlights.");
    if (s.brightnessOverride) {
        SliderF("Shadows", &s.shadowsMul, 0.f, 4.f, "%.2fx", 1.f);
        SliderF("Mid-tones", &s.midtonesMul, 0.f, 3.f, "%.2fx", 1.f);
        SliderF("Highlights", &s.highlightsMul, 0.f, 3.f, "%.2fx", 1.f);
        SliderF("Colour (desaturation)", &s.desaturationMul, 0.f, 3.f, "%.2fx", 1.f);
    }
    Toggle("Player light", &s.darkLightOverride, "A soft light around you, so total darkness is not pitch black.");
    if (s.darkLightOverride) {
        SliderF("Light radius", &s.darkLightRadius, 50.f, 3000.f, "%.0f", 600.f);
        SliderF("Light brightness", &s.darkLightBrightness, 0.f, 2.f, "%.2f", 0.15f);
    }

    Heading("Screen effects");
    Toggle("No film grain", &s.noFilmGrain);
    Toggle("No vignette", &s.noVignette, "Removes the dark edges of the camcorder image.");
    Toggle("No damage effect", &s.noHurtEffect, "No red blur when hurt.");
    Toggle("Colour tint", &s.tintOverride);
    if (s.tintOverride) ImGui::ColorEdit4("Tint", s.tint, ImGuiColorEditFlags_Float);

    Heading("HUD");
    Toggle("Hide HUD", &s.hideHud, "Great for screenshots and videos.");
    Toggle("Hide crosshair dot", &s.hideCrosshair);

    Heading("Frame rate");
    Toggle("Frame-rate limit", &s.fpsLimitOverride, "Outlast normally caps the frame rate at 62.");
    if (s.fpsLimitOverride) SliderF("Max FPS", &s.fpsLimit, 30.f, 300.f, "%.0f", 62.f);
}

void PageEsp(Ctx& c) {
    ModState& s = c.s;
    Toggle("ESP enabled", &s.espEnabled, "Shows where things are through walls.");
    int counts[static_cast<int>(EspCategory::Count)] = {};
    for (const EspItem& it : c.snap.esp) ++counts[static_cast<int>(it.category)];

    Heading("Show");
    for (int i = 0; i < static_cast<int>(EspCategory::Count); ++i) {
        EspStyle& st = s.esp[i];
        ImGui::PushID(i);
        ImGui::ColorEdit4("##col", st.color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::SameLine();
        ImGui::Checkbox(EspCategoryName(static_cast<EspCategory>(i)), &st.enabled);
        if (s.espEnabled && st.enabled) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d)", counts[i]);
        }
        ImGui::PopID();
    }
    Hint("Recording spots are the places where Miles writes a note after filming something - you need all of "
         "them, plus every document, for the collectible achievements.");

    Heading("Options");
    SliderF("Max distance", &s.espMaxDistance, 500.f, 30000.f, "%.0f", 6000.f, "In game units (about 50 per metre).");
    Toggle("Labels", &s.espShowLabels);
    Toggle("Distances", &s.espShowDistance);
    Toggle("Tracer lines", &s.espTracers);
    Toggle("Arrows for things behind you", &s.espOffscreenArrows);
    Toggle("Hide things already collected", &s.espHideCollected);
}

void PagePerformance(Ctx& c) {
    ModState& s = c.s;
    FrameStats fs = GetFrameStats();
    ImGui::Text("%.0f FPS", render::FrameRate());
    ImGui::SameLine();
    ImGui::TextDisabled("(mod uses %.2f ms per frame)", fs.modMsAvg);

    Heading("Performance mode (instant)");
    const char* levels[] = {"Off", "Balanced - small quality loss", "Potato - for very weak PCs"};
    for (int i = 0; i < 3; ++i) ImGui::RadioButton(levels[i], &s.perfLevel, i);
    Hint("Lowers shadows, effects and the 3D resolution while the game runs, using the engine's own "
         "scalability settings. If a setting can't change while playing it takes effect after a restart with "
         "the INI presets below.");
    std::vector<std::string> report = perf::LastReport();
    if (!report.empty() && ImGui::TreeNode("What changed")) {
        for (const std::string& l : report) ImGui::TextUnformatted(l.c_str());
        ImGui::TreePop();
    }

    Heading("Permanent presets (OLSystemSettings.ini, apply on restart)");
    static std::string message;
    for (const ini::Preset& p : ini::Presets()) {
        if (std::string(p.name) != "Laptop / low-end PC" && std::string(p.name) != "Potato" &&
            std::string(p.name) != "High quality")
            continue;
        if (ImGui::Button(p.name)) {
            std::string err;
            int n = ini::ApplyPreset(p, err);
            message = err.empty() ? str::Format("Applied %d tweaks - restart Outlast to use them.", n) : "Error: " + err;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", p.description);
    }
    if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
    Hint("Every change can be undone on the INI Tweaks page, and your original files are backed up in "
         "OutlastModMenu\\ini_backup.");

    Heading("More tips for laptops");
    ImGui::BulletText("Plug the laptop in and use the 'High performance' power plan.");
    ImGui::BulletText("On laptops with two GPUs, make OLGame.exe use the dedicated GPU (NVIDIA Control Panel or "
                      "Windows Graphics settings).");
    ImGui::BulletText("Lower the resolution in the game's options; with 'Render at 75%%' this stacks.");
    ImGui::BulletText("Close browsers and overlays; Outlast is a 32/64-bit DirectX 9/11 game and likes free RAM.");
    ImGui::BulletText("Turn VSync off if the frame rate halves when it drops below 60.");
}

}  // namespace omm::ui
