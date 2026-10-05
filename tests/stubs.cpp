// Minimal stand-ins for game-thread functions that the file-handling code
// links against but the tests never reach (they need a running game).
#include "../src/game/outlast.h"

namespace omm::game {

const World& W() {
    static World w;
    return w;
}

std::string Console(const std::string&) { return std::string(); }

}  // namespace omm::game
