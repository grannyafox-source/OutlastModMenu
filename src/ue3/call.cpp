#include "call.h"

#include "../core/log.h"
#include "../core/memory.h"
#include "../core/strutil.h"

#if OMM_WINDOWS

namespace omm::ue3 {

namespace {
volatile uint32_t g_gameThread = 0;
int g_peIndex = -1;
uintptr_t g_peAddr = 0;

#if OMM_X64
using ProcessEventFn = void (*)(UObject*, UFunction*, void*, void*);
#else
using ProcessEventFn = void(OMM_THISCALL*)(UObject*, UFunction*, void*, void*);
#endif

const mem::ModuleInfo& MainModule() {
    static mem::ModuleInfo info = mem::GetMainModule();
    return info;
}

// Keeps the SEH frame free of C++ objects (required by MSVC).
bool GuardedProcessEvent(ProcessEventFn fn, UObject* obj, UFunction* func, void* params) {
#if defined(_MSC_VER)
    __try {
        fn(obj, func, params, nullptr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    fn(obj, func, params, nullptr);
    return true;
#endif
}

#if OMM_X64
// Resolves chained unwind info so we get the primary function start.
uintptr_t FunctionStartX64(uintptr_t addr) {
    DWORD64 imageBase = 0;
    PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(static_cast<DWORD64>(addr), &imageBase, nullptr);
    for (int hop = 0; rf && hop < 8; ++hop) {
        const uint8_t* ui = reinterpret_cast<const uint8_t*>(imageBase + rf->UnwindData);
        if (!mem::IsReadable(ui, 4)) break;
        uint8_t flags = ui[0] >> 3;
        if (!(flags & UNW_FLAG_CHAININFO)) break;
        size_t codes = (static_cast<size_t>(ui[2]) + 1) & ~static_cast<size_t>(1);
        rf = reinterpret_cast<PRUNTIME_FUNCTION>(const_cast<uint8_t*>(ui) + 4 + codes * 2);
    }
    return rf ? static_cast<uintptr_t>(imageBase + rf->BeginAddress) : 0;
}
#endif
}  // namespace

void SetGameThread(uint32_t threadId) { g_gameThread = threadId; }
bool OnGameThread() { return g_gameThread != 0 && GetCurrentThreadId() == g_gameThread; }
bool ProcessEventReady() { return g_peIndex >= 0; }
int ProcessEventIndex() { return g_peIndex; }
uintptr_t ProcessEventAddress() { return g_peAddr; }

namespace {
struct VtableMatch {
    int exactIdx = -1;
    int bestIdx = -1;
    uintptr_t best = 0;
};

VtableMatch SearchVtable(void** vtbl, uintptr_t returnAddress, uintptr_t exact) {
    const mem::ModuleInfo& mod = MainModule();
    VtableMatch m;
    for (int i = 0; i < 400; ++i) {
        if (!mem::IsReadable(&vtbl[i], sizeof(void*))) break;
        uintptr_t e = reinterpret_cast<uintptr_t>(vtbl[i]);
        if (!mod.InCode(e)) {
            if (i > 16) break;  // left the vtable
            continue;
        }
        if (exact && e == exact && m.exactIdx < 0) m.exactIdx = i;
        if (e <= returnAddress && returnAddress - e < 0x4000 && e > m.best) {
            m.best = e;
            m.bestIdx = i;
        }
    }
    return m;
}
}  // namespace

void DiscoverProcessEvent(UObject* self, uintptr_t returnAddress) {
    if (g_peIndex >= 0 || !self) return;
    uintptr_t exact = 0;
#if OMM_X64
    exact = FunctionStartX64(returnAddress);
#endif
    // The hooked function was called from UObject::ProcessEvent. Actors
    // override ProcessEvent (AActor::ProcessEvent forwards to the UObject
    // version), so search the vtable of the object's class first: UClass
    // does not override it, so its slot holds UObject::ProcessEvent itself.
    // The slot index is the same for every object.
    UObject* candidates[2] = {ClassOf(self), self};
    for (UObject* obj : candidates) {
        if (!obj || !mem::IsReadable(obj, sizeof(void*))) continue;
        void** vtbl = *reinterpret_cast<void***>(obj);
        if (!mem::IsReadable(vtbl, sizeof(void*))) continue;
        VtableMatch m = SearchVtable(vtbl, returnAddress, exact);
        int idx = m.exactIdx >= 0 ? m.exactIdx : m.bestIdx;
        if (idx < 0) continue;
        g_peAddr = reinterpret_cast<uintptr_t>(vtbl[idx]);
        g_peIndex = idx;
        LOGI("ProcessEvent: vtable index %d at %p (return address %p, %s, %s vtable)", idx,
             reinterpret_cast<void*>(g_peAddr), reinterpret_cast<void*>(returnAddress),
             m.exactIdx >= 0 ? "unwind-table match" : "nearest vtable entry", obj == self ? "object" : "class");
        return;
    }
    LOGE("ProcessEvent discovery failed (return address %p)", reinterpret_cast<void*>(returnAddress));
}

bool ProcessEvent(UObject* obj, UFunction* fn, void* params) {
    if (g_peIndex < 0 || !obj || !fn) return false;
    if (!OnGameThread()) {
        LOGE("ProcessEvent(%s) called off the game thread - ignored", Name(fn).c_str());
        return false;
    }
    if (!IsValid(obj) || !IsValid(fn)) return false;
    void** vtbl = *reinterpret_cast<void***>(obj);
    auto pe = reinterpret_cast<ProcessEventFn>(vtbl[g_peIndex]);
    uint16_t* native = L().funcNative >= 0 ? reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(fn) + L().funcNative)
                                            : nullptr;
    uint16_t saved = native ? *native : 0;
    if (saved) *native = 0;
    bool ok = GuardedProcessEvent(pe, obj, fn, params);
    if (saved) *native = saved;
    if (!ok) LOGE("Exception inside %s", FullName(fn).c_str());
    return ok;
}

// ---------------------------------------------------------------------------

Call::Call(UObject* obj, const char* function) : obj_(obj) {
    if (obj_ && IsValid(obj_)) fn_ = FindFunction(ClassOf(obj_), function);
    if (!fn_) error_ = std::string("function not found: ") + function;
    Prepare();
}

Call::Call(UObject* obj, UFunction* fn) : obj_(obj), fn_(fn) {
    if (!fn_) error_ = "null function";
    Prepare();
}

void Call::Prepare() {
    if (!fn_) return;
    size_t size = 16;
    for (UField* f : Fields(fn_, false)) {
        if (!IsProperty(f)) continue;
        int32_t end = PropOffset(f) + PropElementSize(f) * std::max(1, PropArrayDim(f));
        if (end > 0 && static_cast<size_t>(end) > size) size = static_cast<size_t>(end);
    }
    if (L().funcParmsSize >= 0) {
        uint16_t ps = 0;
        std::memcpy(&ps, reinterpret_cast<uint8_t*>(fn_) + L().funcParmsSize, 2);
        if (ps > size) size = ps;
    }
    buf_.assign((size + 15) & ~static_cast<size_t>(15), 0);
}

bool Call::HasParam(const char* param) const { return fn_ && FindProperty(fn_, param); }

uint8_t* Call::Param(const char* name, const char* type, UProperty** outProp) {
    if (!fn_) return nullptr;
    UProperty* p = FindProperty(fn_, name);
    if (!p) {
        error_ = std::string("no parameter ") + name;
        return nullptr;
    }
    if (type && PropType(p) != type) {
        // Object-like properties are interchangeable for our purposes.
        bool objLike = std::strcmp(type, "ObjectProperty") == 0 &&
                       (PropType(p) == "ClassProperty" || PropType(p) == "ComponentProperty" ||
                        PropType(p) == "InterfaceProperty");
        if (!objLike) {
            error_ = std::string("parameter ") + name + " is " + PropType(p) + ", expected " + type;
            return nullptr;
        }
    }
    int32_t off = PropOffset(p);
    if (off < 0 || static_cast<size_t>(off) + static_cast<size_t>(std::max(1, PropElementSize(p))) > buf_.size()) {
        error_ = std::string("bad offset for ") + name;
        return nullptr;
    }
    if (outProp) *outProp = p;
    return buf_.data() + off;
}

Call& Call::Float(const char* param, float v) {
    if (uint8_t* p = Param(param, "FloatProperty")) std::memcpy(p, &v, 4);
    return *this;
}
Call& Call::Int(const char* param, int32_t v) {
    if (uint8_t* p = Param(param, "IntProperty")) std::memcpy(p, &v, 4);
    return *this;
}
Call& Call::Byte(const char* param, uint8_t v) {
    if (uint8_t* p = Param(param, "ByteProperty")) *p = v;
    return *this;
}
Call& Call::Bool(const char* param, bool v) {
    UProperty* prop = nullptr;
    if (uint8_t* p = Param(param, "BoolProperty", &prop)) {
        uint32_t bits;
        std::memcpy(&bits, p, 4);
        uint32_t mask = PropBoolMask(prop);
        bits = v ? (bits | mask) : (bits & ~mask);
        std::memcpy(p, &bits, 4);
    }
    return *this;
}
Call& Call::Obj(const char* param, UObject* v) {
    if (uint8_t* p = Param(param, "ObjectProperty")) std::memcpy(p, &v, sizeof(v));
    return *this;
}
Call& Call::NameP(const char* param, const FName& v) {
    if (uint8_t* p = Param(param, "NameProperty")) std::memcpy(p, &v, sizeof(v));
    return *this;
}
Call& Call::Str(const char* param, const std::string& utf8) {
    if (uint8_t* p = Param(param, "StrProperty")) {
        auto s = std::make_unique<std::u16string>(str::Utf8ToUtf16(utf8));
        FString fs;
        fs.Data = &(*s)[0];
        fs.Num = static_cast<int32_t>(s->size() + 1);  // includes the terminating NUL
        fs.Max = fs.Num;
        std::memcpy(p, &fs, sizeof(fs));
        strings_.push_back(std::move(s));
    }
    return *this;
}
Call& Call::Vector(const char* param, const FVector& v) {
    if (uint8_t* p = Param(param, "StructProperty")) std::memcpy(p, &v, sizeof(v));
    return *this;
}
Call& Call::Rotator(const char* param, const FRotator& v) {
    if (uint8_t* p = Param(param, "StructProperty")) std::memcpy(p, &v, sizeof(v));
    return *this;
}
Call& Call::Raw(const char* param, const void* data, size_t size) {
    UProperty* prop = nullptr;
    if (uint8_t* p = Param(param, nullptr, &prop)) {
        size_t cap = static_cast<size_t>(std::max(1, PropElementSize(prop)) * std::max(1, PropArrayDim(prop)));
        std::memcpy(p, data, std::min(size, cap));
    }
    return *this;
}

bool Call::Invoke() {
    if (!Ok() || !error_.empty()) {
        if (!error_.empty()) LOGW("Call %s: %s", fn_ ? Name(fn_).c_str() : "?", error_.c_str());
        return false;
    }
    return ProcessEvent(obj_, fn_, buf_.data());
}

float Call::OutFloat(const char* param) {
    float v = 0;
    if (uint8_t* p = Param(param, "FloatProperty")) std::memcpy(&v, p, 4);
    return v;
}
int32_t Call::OutInt(const char* param) {
    int32_t v = 0;
    if (uint8_t* p = Param(param, "IntProperty")) std::memcpy(&v, p, 4);
    return v;
}
bool Call::OutBool(const char* param) {
    UProperty* prop = nullptr;
    uint32_t bits = 0;
    if (uint8_t* p = Param(param, "BoolProperty", &prop)) std::memcpy(&bits, p, 4);
    return prop && (bits & PropBoolMask(prop)) != 0;
}
UObject* Call::OutObj(const char* param) {
    UObject* v = nullptr;
    if (uint8_t* p = Param(param, "ObjectProperty")) std::memcpy(&v, p, sizeof(v));
    return v;
}
std::string Call::OutStr(const char* param) {
    FString s;
    if (uint8_t* p = Param(param, "StrProperty")) {
        std::memcpy(&s, p, sizeof(s));
        // Engine-allocated return strings are intentionally leaked: they were
        // allocated by the game's allocator, which the mod cannot call.
        return FStringToUtf8(s);
    }
    return std::string();
}
FVector Call::OutVector(const char* param) {
    FVector v;
    if (uint8_t* p = Param(param, "StructProperty")) std::memcpy(&v, p, sizeof(v));
    return v;
}
FRotator Call::OutRotator(const char* param) {
    FRotator v;
    if (uint8_t* p = Param(param, "StructProperty")) std::memcpy(&v, p, sizeof(v));
    return v;
}

bool CallNoArgs(UObject* obj, const char* function) {
    Call c(obj, function);
    return c.Ok() && c.Invoke();
}

}  // namespace omm::ue3

#endif  // OMM_WINDOWS
