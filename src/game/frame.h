// Per-frame driver. The loader hooks GameViewportClient.PostRender, which
// the engine calls once per rendered frame on the game thread - in menus,
// in game and while paused.
#pragma once

#include "../ue3/types.h"

#include <cstdint>
#include <string>

namespace omm::game {

void OnGameFrame(ue3::UObject* viewportClient);

// Frame-time statistics for the Diagnostics tab.
struct FrameStats {
    uint64_t frames = 0;
    float modMs = 0.f;     // time the mod spent in the last frame
    float modMsAvg = 0.f;  // smoothed
};
FrameStats GetFrameStats();

}  // namespace omm::game
