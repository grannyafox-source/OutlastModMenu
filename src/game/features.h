// Per-frame feature application (game thread).
#pragma once

#include "state.h"

namespace omm::game {

void ApplyPlayerFeatures(const ModState& s);
void ApplyVisualFeatures(const ModState& s);
void ApplyWorldFeatures(const ModState& s, float dt);
void ApplyEnemyFeatures(const ModState& s, float dt);
void CollectEsp(const ModState& s, Snapshot& snap);
void FillPlayerSnapshot(Snapshot& snap);

}  // namespace omm::game
