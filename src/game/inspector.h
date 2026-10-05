// Object inspector for the Diagnostics page: looks up live engine objects
// and lists their properties. Requests run on the game thread; the UI reads
// the last result.
#pragma once

#include <string>
#include <vector>

namespace omm::game::inspector {

struct Result {
    std::string title;
    std::vector<std::string> lines;
};

// Render thread: queue a request.
void InspectObject(const std::string& name, const std::string& className);  // name may be "Hero", "PC", "WorldInfo"
void ListInstances(const std::string& className);
void ListFunctions(const std::string& className);

Result Last();  // any thread

}  // namespace omm::game::inspector
