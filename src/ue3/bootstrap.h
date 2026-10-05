// Runs the layout scanner against the running game.
#pragma once

#include <string>

namespace omm::ue3 {

// Returns true once the engine tables and layout have been found. Safe to
// call repeatedly while the game is still starting up.
bool Bootstrap(std::string& status);

}  // namespace omm::ue3
