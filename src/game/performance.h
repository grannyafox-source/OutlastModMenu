// Runtime performance mode (no restart): lowers engine scalability settings
// with the engine's own "scale set" console command and switches off the
// most expensive post-process passes. The permanent version lives in
// initweaks.h (OLSystemSettings.ini presets).
#pragma once

#include <string>
#include <vector>

namespace omm::game::perf {

enum class Level { Off = 0, Balanced, Potato };
const char* LevelName(Level l);

// Game thread. Applies the level (and restores the previous values when
// going back to Off). Safe to call every frame: only acts on changes.
void Update(Level wanted);
void Reapply();  // after a level change (post-process chains are recreated)

// Lines printed by the engine for the last change, for the UI.
std::vector<std::string> LastReport();

}  // namespace omm::game::perf
