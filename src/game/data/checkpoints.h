// Checkpoint names of Outlast and Outlast: Whistleblower, grouped by chapter.
//
// The names are the game's own (as used by OLGame.ini, e.g.
// FirstFingerlessCheckpoint=Male_TortureDone). Order inside a chapter is the
// approximate story order; the mod also reads the authoritative list from
// the game's OLCheckpointList actor when it is loaded.
#pragma once

#include <string>
#include <vector>

namespace omm::data {

struct Checkpoint {
    const char* name;
    const char* note;  // nullptr when there is nothing notable to say
};

struct Chapter {
    const char* title;  // in-game location name
    bool dlc;
    std::vector<Checkpoint> checkpoints;
};

const std::vector<Chapter>& Chapters();

// Memorable story moments -> the checkpoint that starts just before them.
struct Scene {
    const char* title;
    const char* checkpoint;
    const char* description;
    bool dlc;
};
const std::vector<Scene>& Scenes();

bool IsDlcCheckpoint(const std::string& name);

}  // namespace omm::data
