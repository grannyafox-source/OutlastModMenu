// Brightness and visual tweaks.
#include "features.h"
#include "outlast.h"

#include "../ue3/call.h"

#include <cmath>

namespace omm::game {

using namespace ue3;

namespace {
UObject* ClientObject() {
    // UEngine::Client (WindowsClient) holds DisplayGamma.
    return Obj(W().engine, "Client");
}
}  // namespace

void ApplyVisualFeatures(const ModState& s) {
    const World& w = W();
    OverrideStore& o = Overrides();

    // Gamma: prefer the engine property (instant, not saved); fall back to
    // the console command for builds where it is not exposed.
    UObject* client = ClientObject();
    static bool g_gammaViaConsole = false;
    static float g_lastConsoleGamma = -1.f;
    if (client && Prop(client, "DisplayGamma")) {
        o.Float("gamma", client, "DisplayGamma", s.gammaOverride, s.gamma);
    } else if (s.gammaOverride) {
        if (std::fabs(g_lastConsoleGamma - s.gamma) > 0.001f) {
            Console("gamma " + std::to_string(s.gamma));
            g_lastConsoleGamma = s.gamma;
            g_gammaViaConsole = true;
        }
    } else if (g_gammaViaConsole) {
        Console("gamma 2.2");
        g_gammaViaConsole = false;
        g_lastConsoleGamma = -1.f;
    }

    // LocalPlayer post-process multipliers ([Engine.Player] in OLEngine.ini).
    UObject* lp = w.localPlayer;
    o.Float("pp:shadows", lp, "PP_ShadowsMultiplier", s.brightnessOverride, s.shadowsMul);
    o.Float("pp:midtones", lp, "PP_MidTonesMultiplier", s.brightnessOverride, s.midtonesMul);
    o.Float("pp:highlights", lp, "PP_HighlightsMultiplier", s.brightnessOverride, s.highlightsMul);
    o.Float("pp:desat", lp, "PP_DesaturationMultiplier", s.brightnessOverride, s.desaturationMul);

    // The hero's "dark light" lets you see a little in total darkness; the
    // game exposes an override for scripted sequences.
    UObject* hero = w.hero;
    o.Bool("darkLight", hero, "bOverrideDarkLight", s.darkLightOverride, true);
    o.Float("darkLightR", hero, "DarkLightOverrideRadius", s.darkLightOverride, s.darkLightRadius);
    o.Float("darkLightB", hero, "DarkLightOverrideBrightness", s.darkLightOverride, s.darkLightBrightness);

    // Outlast's camcorder/uber post-process effect.
    UObject* uber = Obj(w.fx, "CurrentUberPostEffect");
    o.Float("grain", uber, "GrainOpacity", s.noFilmGrain, 0.f);
    o.Bool("grainFlag", w.fx, "bGrainDisabled", s.noFilmGrain, true);
    o.Float("vignette", uber, "VignetteBlack", s.noVignette, 0.f);
    o.Float("hurt", uber, "HurtScale", s.noHurtEffect, 0.f);
    o.Float("hurtAmount", uber, "HurtAmount", s.noHurtEffect, 0.f);
    FLinearColor tint{s.tint[0], s.tint[1], s.tint[2], s.tint[3]};
    o.Raw("tint", uber, "CameraColor", s.tintOverride, &tint, sizeof(tint));

    // HUD.
    o.Bool("hud", w.hud, "bShowHUD", s.hideHud, false);
    o.Bool("crosshair", w.hud, "bShowCrosshair", s.hideCrosshair, false);

    // Frame-rate cap (engine frame smoothing).
    o.Float("fps", w.engine, "MaxSmoothedFrameRate", s.fpsLimitOverride, s.fpsLimit);
    o.Bool("fpsSmooth", w.engine, "bSmoothFrameRate", s.fpsLimitOverride, true);
}

}  // namespace omm::game
