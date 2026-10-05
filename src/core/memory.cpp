#include "memory.h"

#if OMM_WINDOWS

#include "strutil.h"

namespace omm::mem {

namespace {
bool ProtectionReadable(DWORD protect) {
    if (protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                       PAGE_EXECUTE_WRITECOPY)) != 0;
}
}  // namespace

bool ProcessOracle::Readable(uintptr_t addr, size_t len) const {
    if (addr < 0x10000 || len == 0) return false;
    uintptr_t end = addr + len;
    if (end < addr) return false;  // overflow
    uintptr_t cur = addr;
    while (cur < end) {
        bool found = false;
        for (int i = 0; i < cacheCount_; ++i) {
            const CachedRegion& r = cache_[i];
            if (cur >= r.begin && cur < r.end) {
                if (!r.readable) return false;
                cur = r.end;
                found = true;
                break;
            }
        }
        if (found) continue;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<LPCVOID>(cur), &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        CachedRegion r;
        r.begin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        r.end = r.begin + mbi.RegionSize;
        r.readable = mbi.State == MEM_COMMIT && ProtectionReadable(mbi.Protect);
        cache_[cacheNext_] = r;
        cacheNext_ = (cacheNext_ + 1) % 8;
        if (cacheCount_ < 8) ++cacheCount_;
        if (!r.readable) return false;
        cur = r.end;
    }
    return true;
}

bool IsReadable(const void* p, size_t len) {
    ProcessOracle o;
    return o.Readable(reinterpret_cast<uintptr_t>(p), len);
}

ModuleInfo GetModuleInfo(HMODULE module) {
    ModuleInfo info;
    if (!module) return info;
    info.base = reinterpret_cast<uintptr_t>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return info;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(info.base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return info;
    info.size = nt->OptionalHeader.SizeOfImage;
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        Section s;
        char name[9] = {};
        std::memcpy(name, sec[i].Name, 8);
        s.name = name;
        s.begin = info.base + sec[i].VirtualAddress;
        DWORD vsize = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        s.end = s.begin + vsize;
        s.characteristics = sec[i].Characteristics;
        info.sections.push_back(s);
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    info.path = str::WideToUtf8(path);
    return info;
}

ModuleInfo GetMainModule() { return GetModuleInfo(GetModuleHandleW(nullptr)); }

bool PatchPointer(void** slot, void* value, void** oldValue) {
    DWORD oldProt = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt)) return false;
    if (oldValue) *oldValue = *slot;
    InterlockedExchangePointer(slot, value);
    DWORD dummy = 0;
    VirtualProtect(slot, sizeof(void*), oldProt, &dummy);
    return true;
}

}  // namespace omm::mem

#endif  // OMM_WINDOWS
