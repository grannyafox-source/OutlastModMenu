#include "performance.h"

#include "initweaks.h"
#include "outlast.h"

#include "../core/ini.h"
#include "../core/log.h"
#include "../core/strutil.h"
#include "../core/sync.h"

namespace omm::game::perf {

using namespace ue3;

namespace {
struct Setting {
    const char* name;  // [SystemSettings] key, also accepted by "scale set"
    const char* balanced;
    const char* potato;
    const char* fallback;  // used to restore when the INI has no value
};

const Setting kSettings[] = {
    {"MotionBlur", "False", "False", "True"},
    {"DepthOfField", "False", "False", "True"},
    {"AmbientOcclusion", "False", "False", "True"},
    {"bAllowLightShafts", "False", "False", "True"},
    {"LensFlares", "False", "False", "True"},
    {"Distortion", nullptr, "False", "True"},
    {"DynamicShadows", nullptr, "False", "True"},
    {"MaxShadowResolution", "512", "256", "1120"},
    {"DetailMode", "1", "0", "2"},
    {"ParticleLODBias", "1", "2", "0"},
    {"SkeletalMeshLODBias", nullptr, "1", "0"},
    {"ScreenPercentage", "85", "65", "100"},
};

// Post-process effect classes that are expensive and purely cosmetic.
const char* const kExpensiveEffects[] = {"AmbientOcclusionEffect", "MotionBlurEffect"};

Level g_applied = Level::Off;
Mutex g_reportLock;
std::vector<std::string> g_report;

void Report(const std::string& line) {
    LockGuard lock(g_reportLock);
    g_report.push_back(line);
    if (g_report.size() > 60) g_report.erase(g_report.begin());
}

void Scale(const char* name, const std::string& value) {
    std::string out = Console(str::Format("scale set %s %s", name, value.c_str()));
    out = str::Trim(out);
    Report(str::Format("%s = %s%s%s", name, value.c_str(), out.empty() ? "" : "  -> ", out.c_str()));
}

void ApplyPostProcess(bool disable) {
    UObject* lp = W().localPlayer;
    std::vector<UObject*> chains;
    if (UObject* chain = Obj(lp, "PlayerPostProcess")) chains.push_back(chain);
    std::vector<UObject*> more;
    if (GetObjArray(lp, "PlayerPostProcessChains", more)) chains.insert(chains.end(), more.begin(), more.end());
    for (UObject* chain : chains) {
        std::vector<UObject*> effects;
        if (!chain || !GetObjArray(chain, "Effects", effects)) continue;
        for (UObject* fx : effects) {
            if (!fx || !IsValid(fx)) continue;
            bool expensive = false;
            for (const char* cls : kExpensiveEffects) expensive |= IsA(fx, cls);
            if (!expensive) continue;
            Overrides().Bool("perf:pp:" + std::to_string(IndexOf(fx)), fx, "bShowInGame", disable, false);
        }
    }
}
}  // namespace

const char* LevelName(Level l) {
    switch (l) {
        case Level::Off: return "Off";
        case Level::Balanced: return "Balanced";
        case Level::Potato: return "Potato";
    }
    return "?";
}

void Update(Level wanted) {
    if (wanted == g_applied || !W().pc) return;
    {
        LockGuard lock(g_reportLock);
        g_report.clear();
    }
    IniDocument ini;
    bool haveIni = ini.Load(ini::FilePath(ini::File::SystemSettings));
    for (const Setting& s : kSettings) {
        const char* v = wanted == Level::Balanced ? s.balanced : (wanted == Level::Potato ? s.potato : nullptr);
        if (v) {
            Scale(s.name, v);
        } else if (g_applied != Level::Off) {
            // Back to what the game started with.
            std::string orig = haveIni ? ini.Get("SystemSettings", s.name).value_or(s.fallback) : s.fallback;
            Scale(s.name, orig);
        }
    }
    ApplyPostProcess(wanted != Level::Off);
    LOGI("Performance mode: %s -> %s", LevelName(g_applied), LevelName(wanted));
    g_applied = wanted;
}

void Reapply() {
    if (g_applied != Level::Off) ApplyPostProcess(true);
}

std::vector<std::string> LastReport() {
    LockGuard lock(g_reportLock);
    return g_report;
}

}  // namespace omm::game::perf
