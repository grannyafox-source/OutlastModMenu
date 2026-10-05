#include "bootstrap.h"

#include "../core/log.h"
#include "../core/memory.h"
#include "../core/settings.h"
#include "engine.h"
#include "scanner.h"

#if OMM_WINDOWS

namespace omm::ue3 {

namespace {
// Code pointers: inside an executable section of the game, or any executable
// image page (covers executables whose section headers were rewritten by a
// packer or DRM wrapper).
bool IsCodeAddress(const mem::ModuleInfo& mod, uintptr_t a) {
    if (mod.InCode(a)) return true;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        mbi.Type != MEM_IMAGE)
        return false;
    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & exec) != 0;
}
}  // namespace

bool Bootstrap(std::string& status) {
    static mem::ModuleInfo mod = mem::GetMainModule();
    static int attempt = 0;
    static uint64_t partialSince = 0;  // engine found but UFunction layout still unknown
    static std::string lastLogged;
    ++attempt;
    std::vector<mem::Range> ranges;
    for (const mem::Section& s : mod.sections)
        if (s.Writable() && !s.Executable()) ranges.push_back({s.begin, s.end});
    if (ranges.empty()) {
        status = "OLGame.exe has no writable data sections";
        return false;
    }
    mem::ProcessOracle oracle;
    Scanner scanner(oracle, ranges, [](uintptr_t a) { return IsCodeAddress(mod, a); });
    bool ok = scanner.RunAll();
    const ScanResult& r = scanner.Result();
    std::string reason = r.notes.empty() ? std::string("scanning...") : r.notes.back();

    // Script functions get their code pointers while the engine finishes
    // loading, so keep scanning for a while before giving up on them.
    bool functionsMissing = ok && r.layout.funcFunc < 0;
    bool waitingForFunctions = false;
    if (functionsMissing) {
        if (!partialSince) partialSince = NowMs();
        if (NowMs() - partialSince < 120000) {
            ok = false;
            waitingForFunctions = true;
            reason = "engine found, waiting for its script functions to load";
        }
    }

    // Log the scan report only when the outcome changes (or every 100
    // attempts), so neither a slow start nor the later rescans for the
    // function layout flood the log.
    std::string key = ok                  ? std::string(functionsMissing ? "ok, no functions" : "ok")
                      : waitingForFunctions ? reason
                      : r.notes.empty()     ? reason
                                            : r.notes.back();
    bool changed = key != lastLogged;
    if (changed || attempt % 100 == 1) {
        for (const std::string& n : r.notes) LOGI("[scan %d] %s", attempt, n.c_str());
        lastLogged = key;
    }
    if (!ok) {
        status = reason;
        return false;
    }
    if (!Init(r)) {
        status = "engine layout incomplete";
        return false;
    }
    if (changed) {
        if (functionsMissing)
            LOGE("UFunction layout still unknown after two minutes - functions cannot be called or hooked yet. "
                 "Please send this log.");
        LOGI("Engine layout: %s", r.layout.Describe().c_str());
    }
    status = "ready";
    return true;
}

}  // namespace omm::ue3

#endif
