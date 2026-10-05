#include "hooks.h"

#include "../core/log.h"
#include "../core/memory.h"
#include "call.h"

#if OMM_WINDOWS

#include "MinHook.h"

#if defined(_MSC_VER)
#include <intrin.h>
#define OMM_RETURN_ADDRESS() reinterpret_cast<uintptr_t>(_ReturnAddress())
#else
#define OMM_RETURN_ADDRESS() reinterpret_cast<uintptr_t>(__builtin_return_address(0))
#endif

namespace omm::ue3 {

namespace {
struct Slot {
    UFunction* fn = nullptr;
    void* original = nullptr;
    HookCallback after = nullptr;
};

constexpr int kSlots = 8;
Slot g_slots[kSlots];
volatile uint64_t g_calls = 0;

#if OMM_X64
using NativeFn = void (*)(UObject*, void*, void*);
#else
using NativeFn = void(OMM_THISCALL*)(UObject*, void*, void*);
#endif

void OnEnter(UObject* self, uintptr_t returnAddress) {
    if (!ProcessEventReady()) {
        SetGameThread(GetCurrentThreadId());
        DiscoverProcessEvent(self, returnAddress);
    }
    ++g_calls;
}

#if OMM_X64
template <int N>
void SlotThunk(UObject* self, void* stack, void* result) {
    OnEnter(self, OMM_RETURN_ADDRESS());
    reinterpret_cast<NativeFn>(g_slots[N].original)(self, stack, result);
    if (g_slots[N].after) g_slots[N].after(self);
}
#else
// __fastcall receives `this` in ECX like __thiscall; EDX is unused.
template <int N>
void OMM_FASTCALL SlotThunk(UObject* self, void* /*edx*/, void* stack, void* result) {
    OnEnter(self, OMM_RETURN_ADDRESS());
    reinterpret_cast<NativeFn>(g_slots[N].original)(self, stack, result);
    if (g_slots[N].after) g_slots[N].after(self);
}
#endif

void* ThunkFor(int i) {
    switch (i) {
        case 0: return reinterpret_cast<void*>(&SlotThunk<0>);
        case 1: return reinterpret_cast<void*>(&SlotThunk<1>);
        case 2: return reinterpret_cast<void*>(&SlotThunk<2>);
        case 3: return reinterpret_cast<void*>(&SlotThunk<3>);
        case 4: return reinterpret_cast<void*>(&SlotThunk<4>);
        case 5: return reinterpret_cast<void*>(&SlotThunk<5>);
        case 6: return reinterpret_cast<void*>(&SlotThunk<6>);
        case 7: return reinterpret_cast<void*>(&SlotThunk<7>);
    }
    return nullptr;
}

void** FuncSlot(UFunction* fn) {
    return reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(fn) + L().funcFunc);
}

// --- ProcessInternal fallback ------------------------------------------------
void* g_piOriginal = nullptr;
UFunction* g_piTarget = nullptr;
HookCallback g_piAfter = nullptr;
int g_frameNodeOffset = -1;
bool g_piActive = false;

int DetectFrameNode(UObject* self, uint8_t* frame) {
    // FFrame derives from FOutputDevice; Node is followed by Object (== this).
    for (int off = static_cast<int>(sizeof(void*)); off <= 0x40; off += static_cast<int>(sizeof(void*))) {
        UObject* node = nullptr;
        UObject* object = nullptr;
        std::memcpy(&node, frame + off, sizeof(node));
        std::memcpy(&object, frame + off + sizeof(void*), sizeof(object));
        if (object == self && IsValid(node) && IsFunction(node)) return off;
    }
    return -1;
}

void HandleProcessInternal(UObject* self, void* stack, uintptr_t returnAddress, bool& isTarget) {
    isTarget = false;
    if (!stack) return;
    uint8_t* frame = static_cast<uint8_t*>(stack);
    if (g_frameNodeOffset < 0) g_frameNodeOffset = DetectFrameNode(self, frame);
    if (g_frameNodeOffset < 0) return;
    UFunction* node = nullptr;
    std::memcpy(&node, frame + g_frameNodeOffset, sizeof(node));
    if (node == g_piTarget) {
        isTarget = true;
        OnEnter(self, returnAddress);
    }
}

#if OMM_X64
void PIDetour(UObject* self, void* stack, void* result) {
    bool isTarget = false;
    HandleProcessInternal(self, stack, OMM_RETURN_ADDRESS(), isTarget);
    reinterpret_cast<NativeFn>(g_piOriginal)(self, stack, result);
    if (isTarget && g_piAfter) g_piAfter(self);
}
#else
void OMM_FASTCALL PIDetour(UObject* self, void* /*edx*/, void* stack, void* result) {
    bool isTarget = false;
    HandleProcessInternal(self, stack, OMM_RETURN_ADDRESS(), isTarget);
    reinterpret_cast<NativeFn>(g_piOriginal)(self, stack, result);
    if (isTarget && g_piAfter) g_piAfter(self);
}
#endif
}  // namespace

bool HookFunction(UFunction* fn, HookCallback after) {
    if (!fn || L().funcFunc < 0 || !IsValid(fn)) return false;
    for (int i = 0; i < kSlots; ++i)
        if (g_slots[i].fn == fn) return true;
    for (int i = 0; i < kSlots; ++i) {
        if (g_slots[i].fn) continue;
        void** slot = FuncSlot(fn);
        // Name lookups use caches owned by the game thread once the hook is
        // live, so resolve the name before swapping the pointer.
        std::string name = FullName(fn);
        g_slots[i].original = *slot;
        g_slots[i].after = after;
        g_slots[i].fn = fn;
        InterlockedExchangePointer(slot, ThunkFor(i));
        LOGI("Hooked %s (Func %p -> slot %d)", name.c_str(), g_slots[i].original, i);
        return true;
    }
    LOGE("No free hook slot for %s", FullName(fn).c_str());
    return false;
}

bool IsHooked(UFunction* fn) {
    for (const Slot& s : g_slots)
        if (s.fn == fn) return true;
    return false;
}

void UnhookAll() {
    for (Slot& s : g_slots) {
        if (!s.fn) continue;
        if (IsValid(s.fn)) InterlockedExchangePointer(FuncSlot(s.fn), s.original);
        s = Slot{};
    }
    if (g_piActive) {
        MH_DisableHook(MH_ALL_HOOKS);
        g_piActive = false;
    }
}

bool InstallProcessInternalHook(UFunction* target, HookCallback after) {
    uintptr_t pi = Scan().processInternal;
    if (!pi || g_piActive) return g_piActive;
    g_piTarget = target;
    g_piAfter = after;
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (MH_CreateHook(reinterpret_cast<void*>(pi), reinterpret_cast<void*>(&PIDetour), &g_piOriginal) != MH_OK) return false;
    std::string name = FullName(target);
    if (MH_EnableHook(reinterpret_cast<void*>(pi)) != MH_OK) return false;
    g_piActive = true;
    LOGW("Using ProcessInternal fallback hook for %s", name.c_str());
    return true;
}

bool ProcessInternalHookActive() { return g_piActive; }
uint64_t HookCalls() { return g_calls; }

}  // namespace omm::ue3

#endif  // OMM_WINDOWS
