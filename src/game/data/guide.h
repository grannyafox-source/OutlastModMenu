// Content of the "Hints & Guide" tab.
#pragma once

#include <vector>

namespace omm::data {

struct GuideEntry {
    const char* heading;
    // Lines starting with "- " are drawn as bullet points.
    const char* text;
};

struct GuideSection {
    const char* title;
    std::vector<GuideEntry> entries;
};

const std::vector<GuideSection>& Guide();

}  // namespace omm::data
