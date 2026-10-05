#include "bootstrap.h"

#include "../core/log.h"
#include "../core/memory.h"
#include "engine.h"
#include "scanner.h"

#if OMM_WINDOWS

namespace omm::ue3 {

bool Bootstrap(std::string& status) {
    static mem::ModuleInfo mod = mem::GetMainModule();
    static int attempt = 0;
    ++attempt;
    std::vector<mem::Range> ranges;
    for (const mem::Section& s : mod.sections)
        if (s.Writable() && !s.Executable()) ranges.push_back({s.begin, s.end});
    if (ranges.empty()) {
        status = "OLGame.exe has no writable data sections";
        return false;
    }
    mem::ProcessOracle oracle;
    Scanner scanner(oracle, ranges, [](uintptr_t a) { return mod.InCode(a); });
    bool ok = scanner.RunAll();
    const ScanResult& r = scanner.Result();
    // Log the full report on success and only every 10th failed attempt so a
    // slow start-up does not flood the log.
    if (ok || attempt % 10 == 1)
        for (const std::string& n : r.notes) LOGI("[scan %d] %s", attempt, n.c_str());
    if (!ok) {
        status = r.notes.empty() ? "scanning..." : r.notes.back();
        return false;
    }
    if (!Init(r)) {
        status = "engine layout incomplete";
        return false;
    }
    LOGI("Engine layout: %s", r.layout.Describe().c_str());
    status = "ready";
    return true;
}

}  // namespace omm::ue3

#endif
