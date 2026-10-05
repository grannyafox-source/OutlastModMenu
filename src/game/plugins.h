// Plugin loader: DLLs in OutlastModMenu\plugins that export OMM_PluginInit.
#pragma once

#include <string>
#include <vector>

namespace omm::plugins {

struct PluginInfo {
    std::string file;
    std::string name;
    bool loaded = false;
    std::string status;
};

void LoadAll();                // once the engine is ready (any thread except DllMain)
void OnGameFrame();            // game thread
void DrawMenuSections();       // render thread, inside the Plugins tab
std::vector<PluginInfo> List();
bool HasMenuSections();

}  // namespace omm::plugins
