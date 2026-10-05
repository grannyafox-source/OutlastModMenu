// Memory helpers: readability checks, PE section info and pointer patching.
#pragma once

#include "common.h"

#include <string>
#include <vector>

namespace omm::mem {

struct Range {
    uintptr_t begin = 0;
    uintptr_t end = 0;
    bool Contains(uintptr_t a) const { return a >= begin && a < end; }
};

// Answers "may I read len bytes at addr?". The engine scanner only talks to
// this interface so it can be unit-tested against fake memory on Linux.
class Oracle {
public:
    virtual ~Oracle() = default;
    virtual bool Readable(uintptr_t addr, size_t len) const = 0;
};

struct Section {
    std::string name;
    uintptr_t begin = 0;
    uintptr_t end = 0;
    uint32_t characteristics = 0;
    bool Executable() const { return (characteristics & 0x20000000u) != 0; }  // IMAGE_SCN_MEM_EXECUTE
    bool Writable() const { return (characteristics & 0x80000000u) != 0; }    // IMAGE_SCN_MEM_WRITE
};

struct ModuleInfo {
    uintptr_t base = 0;
    size_t size = 0;
    std::string path;
    std::vector<Section> sections;
    bool Contains(uintptr_t a) const { return a >= base && a < base + size; }
    bool InCode(uintptr_t a) const {
        for (const Section& s : sections)
            if (s.Executable() && a >= s.begin && a < s.end) return true;
        return false;
    }
};

#if OMM_WINDOWS
// Oracle backed by VirtualQuery with a small region cache.
class ProcessOracle : public Oracle {
public:
    bool Readable(uintptr_t addr, size_t len) const override;
    void Reset() { cacheCount_ = 0; }

private:
    struct CachedRegion {
        uintptr_t begin, end;
        bool readable;
    };
    mutable CachedRegion cache_[8] = {};
    mutable int cacheCount_ = 0;
    mutable int cacheNext_ = 0;
};

// Convenience wrapper (uses a fresh VirtualQuery; fine outside hot loops).
bool IsReadable(const void* p, size_t len);

ModuleInfo GetModuleInfo(HMODULE module);
ModuleInfo GetMainModule();

// Swaps a pointer stored in (possibly read-only) memory, e.g. a vtable slot.
bool PatchPointer(void** slot, void* value, void** oldValue);

// Address of the n-th virtual function of a COM-style object.
inline void** VTableSlot(void* object, size_t index) {
    void** vtbl = *reinterpret_cast<void***>(object);
    return &vtbl[index];
}
#endif

}  // namespace omm::mem
