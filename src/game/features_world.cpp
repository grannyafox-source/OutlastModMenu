// Game speed and enemy-wide modifiers.
#include "enemies.h"
#include "features.h"
#include "outlast.h"

#include "../core/common.h"

namespace omm::game {

using namespace ue3;

void ApplyWorldFeatures(const ModState& s, float /*dt*/) {
    const World& w = W();
    // WorldInfo.TimeDilation scales every actor's DeltaTime; it is what
    // GameInfo.SetGameSpeed and the cheat manager's speed cheats change.
    // Re-applied every frame so cutscenes that touch it cannot undo it.
    float speed = Clamp(s.gameSpeed, 0.05f, 10.f);
    Overrides().Float("gameSpeed", w.worldInfo, "TimeDilation", s.gameSpeedOverride, speed);
}

void ApplyEnemyFeatures(const ModState& s, float dt) { enemies::Apply(s, dt); }

}  // namespace omm::game
