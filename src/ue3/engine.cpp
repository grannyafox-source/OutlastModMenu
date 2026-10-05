#include "engine.h"

#include "../core/log.h"
#include "../core/settings.h"
#include "../core/strutil.h"

#include <unordered_map>

namespace omm::ue3 {

namespace {
constexpr size_t P = sizeof(uintptr_t);

ScanResult g_scan;
bool g_ready = false;
const std::string kEmpty;

// Name string cache (names never change once created).
std::vector<std::string> g_nameCache;
std::vector<uint8_t> g_nameCached;
std::unordered_map<std::string, int32_t> g_nameLookup;
int32_t g_nameLookupBuiltUpTo = 0;

// Class cache.
std::unordered_map<std::string, UClass*> g_classCache;
std::unordered_map<std::string, uint64_t> g_classMiss;  // name -> time of last failed lookup
UClass* g_classClass = nullptr;

struct PathKey {
    const void* type;
    std::string path;
    bool operator==(const PathKey& o) const { return type == o.type && path == o.path; }
};
struct PathKeyHash {
    size_t operator()(const PathKey& k) const {
        return std::hash<const void*>()(k.type) ^ (std::hash<std::string>()(k.path) * 31);
    }
};
struct ResolvedPath {
    int32_t offset = -1;
    UProperty* prop = nullptr;
};
std::unordered_map<PathKey, ResolvedPath, PathKeyHash> g_pathCache;
std::unordered_map<PathKey, UField*, PathKeyHash> g_fieldCache;

template <typename T>
T Rd(const void* base, int off) {
    T v;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + off, sizeof(T));
    return v;
}

uintptr_t NamesData() { return Read<uintptr_t>(g_scan.gnames); }
uintptr_t ObjectsData() { return Read<uintptr_t>(g_scan.gobjects); }
}  // namespace

// ---------------------------------------------------------------------------

bool Init(const ScanResult& scan) {
    g_scan = scan;
    g_ready = scan.gnames && scan.gobjects && scan.layout.ObjectsReady() && scan.layout.StructsReady() &&
              scan.layout.PropertiesReady();
    g_nameCache.clear();
    g_nameCached.clear();
    g_nameLookup.clear();
    g_nameLookupBuiltUpTo = 0;
    g_classCache.clear();
    g_classMiss.clear();
    g_pathCache.clear();
    g_fieldCache.clear();
    g_classClass = nullptr;
    if (g_ready) {
        UObject* objectClass = FindObject("Object", "Class");
        g_classClass = objectClass ? ClassOf(objectClass) : nullptr;
    }
    return g_ready;
}

bool Ready() { return g_ready; }
const Layout& L() { return g_scan.layout; }
const ScanResult& Scan() { return g_scan; }

// ---------------------------------------------------------------------------
// Names

int32_t NamesNum() { return g_ready ? Read<int32_t>(g_scan.gnames + P) : 0; }

const std::string& NameString(int32_t index) {
    if (!g_ready || index < 0 || index >= NamesNum()) return kEmpty;
    if (static_cast<size_t>(index) >= g_nameCache.size()) {
        g_nameCache.resize(static_cast<size_t>(index) + 1024);
        g_nameCached.resize(static_cast<size_t>(index) + 1024, 0);
    }
    if (g_nameCached[index]) return g_nameCache[index];
    uintptr_t e = Read<uintptr_t>(NamesData() + static_cast<uintptr_t>(index) * P);
    std::string s;
    if (e) {
        const Layout& l = g_scan.layout;
        bool wide = false;
        if (l.nameIndexField >= 0 && l.nameIndexShifted) wide = (Read<int32_t>(e + l.nameIndexField) & 1) != 0;
        if (wide) {
            const char16_t* w = reinterpret_cast<const char16_t*>(e + l.nameString);
            size_t n = 0;
            while (n < 1024 && w[n]) ++n;
            s = str::Utf16ToUtf8(w, n);
        } else {
            const char* a = reinterpret_cast<const char*>(e + l.nameString);
            size_t n = 0;
            while (n < 1024 && a[n]) ++n;
            s.assign(a, n);
        }
    }
    g_nameCache[index] = s;
    g_nameCached[index] = 1;
    return g_nameCache[index];
}

std::string NameToString(const FName& n) {
    const std::string& base = NameString(n.Index);
    if (n.Number > 0) return base + "_" + std::to_string(n.Number - 1);
    return base;
}

namespace {
// UE3 stores "Foo_3" as name "Foo" with Number 4 (number + 1). A suffix with
// a leading zero ("Male_ward_01") is not split.
void SplitNumber(const std::string& s, std::string& base, int32_t& number) {
    base = s;
    number = 0;
    size_t us = s.find_last_of('_');
    if (us == std::string::npos || us + 1 >= s.size() || us == 0) return;
    std::string digits = s.substr(us + 1);
    if (digits.size() > 9 || (digits.size() > 1 && digits[0] == '0')) return;
    for (char c : digits)
        if (c < '0' || c > '9') return;
    base = s.substr(0, us);
    number = std::stoi(digits) + 1;
}

int32_t LookupName(const std::string& lowerKey) {
    auto it = g_nameLookup.find(lowerKey);
    if (it != g_nameLookup.end()) return it->second;
    int32_t n = NamesNum();
    if (g_nameLookupBuiltUpTo < n) {
        for (int32_t i = g_nameLookupBuiltUpTo; i < n; ++i) {
            const std::string& ns = NameString(i);
            if (!ns.empty()) g_nameLookup.emplace(str::ToLower(ns), i);
        }
        g_nameLookupBuiltUpTo = n;
        it = g_nameLookup.find(lowerKey);
        if (it != g_nameLookup.end()) return it->second;
    }
    return -1;
}
}  // namespace

bool FindName(const std::string& s, FName& out) {
    if (!g_ready || s.empty()) return false;
    std::string lower = str::ToLower(s);
    std::string base;
    int32_t number = 0;
    SplitNumber(lower, base, number);
    int32_t idx = LookupName(base);
    if (idx < 0 && number) {  // fall back to the unsplit spelling
        idx = LookupName(lower);
        number = 0;
    }
    if (idx < 0) return false;
    out.Index = idx;
    out.Number = number;
    return true;
}

// ---------------------------------------------------------------------------
// Objects

int32_t ObjectsNum() { return g_ready ? Read<int32_t>(g_scan.gobjects + P) : 0; }

UObject* ObjectAt(int32_t index) {
    if (!g_ready || index < 0 || index >= ObjectsNum()) return nullptr;
    return reinterpret_cast<UObject*>(Read<uintptr_t>(ObjectsData() + static_cast<uintptr_t>(index) * P));
}

int32_t IndexOf(const UObject* o) { return o ? Rd<int32_t>(o, g_scan.layout.objIndex) : -1; }

bool IsValid(const UObject* o) {
    if (!g_ready || !o) return false;
    if (reinterpret_cast<uintptr_t>(o) % P) return false;
    int32_t idx = IndexOf(o);
    return idx >= 0 && idx < ObjectsNum() && ObjectAt(idx) == o;
}

UClass* ClassOf(const UObject* o) { return o ? Rd<UClass*>(o, g_scan.layout.objClass) : nullptr; }
UObject* OuterOf(const UObject* o) { return o ? Rd<UObject*>(o, g_scan.layout.objOuter) : nullptr; }
FName NameOf(const UObject* o) { return o ? Rd<FName>(o, g_scan.layout.objName) : FName{}; }
const std::string& Name(const UObject* o) { return o ? NameString(NameOf(o).Index) : kEmpty; }

std::string PathName(const UObject* o) {
    if (!o) return "None";
    std::vector<const UObject*> chain;
    for (const UObject* cur = o; cur && chain.size() < 32; cur = OuterOf(cur)) chain.push_back(cur);
    std::string out;
    for (size_t i = chain.size(); i-- > 0;) {
        if (!out.empty()) out += '.';
        out += NameToString(NameOf(chain[i]));
    }
    return out;
}

std::string FullName(const UObject* o) {
    if (!o) return "None";
    return Name(ClassOf(o)) + " " + PathName(o);
}

bool IsDefaultObject(const UObject* o) {
    const std::string& n = Name(o);
    return n.size() > 9 && n.compare(0, 9, "Default__") == 0;
}

UStruct* SuperOf(const UStruct* s) { return s ? Rd<UStruct*>(s, g_scan.layout.structSuper) : nullptr; }
UField* ChildrenOf(const UStruct* s) { return s ? Rd<UField*>(s, g_scan.layout.structChildren) : nullptr; }
UField* NextOf(const UField* f) { return f ? Rd<UField*>(f, g_scan.layout.fieldNext) : nullptr; }

bool IsChildOf(const UStruct* s, const UStruct* parent) {
    for (int depth = 0; s && depth < 64; ++depth, s = SuperOf(s))
        if (s == parent) return true;
    return false;
}

bool IsA(const UObject* o, const UClass* cls) { return o && cls && IsChildOf(ClassOf(o), cls); }

bool IsA(const UObject* o, const char* className) {
    for (const UStruct* s = o ? ClassOf(o) : nullptr; s; s = SuperOf(s))
        if (Name(s) == className) return true;
    return false;
}

UClass* ClassClass() { return g_classClass; }

UClass* FindClass(const char* name) {
    if (!g_ready) return nullptr;
    auto it = g_classCache.find(name);
    if (it != g_classCache.end()) {
        if (IsValid(it->second) && Name(it->second) == name) return it->second;
        g_classCache.erase(it);
    }
    uint64_t now = NowMs();
    auto miss = g_classMiss.find(name);
    if (miss != g_classMiss.end() && now - miss->second < 2500) return nullptr;
    UObject* found = FindObject(name, "Class");
    if (found) {
        g_classCache[name] = found;
        g_classMiss.erase(name);
    } else {
        g_classMiss[name] = now;
    }
    return found;
}

UObject* FindObject(const char* name, const char* className) {
    FName fn;
    if (!FindName(name, fn)) return nullptr;
    int32_t n = ObjectsNum();
    for (int32_t i = 0; i < n; ++i) {
        UObject* o = ObjectAt(i);
        if (!o) continue;
        FName on = NameOf(o);
        if (on.Index != fn.Index || on.Number != fn.Number) continue;
        if (className && Name(ClassOf(o)) != className) continue;
        return o;
    }
    return nullptr;
}

UObject* FindObjectByPath(const std::string& path, const char* className) {
    std::vector<std::string> parts = str::Split(path, '.');
    if (parts.empty()) return nullptr;
    FName last;
    if (!FindName(parts.back(), last)) return nullptr;
    int32_t n = ObjectsNum();
    for (int32_t i = 0; i < n; ++i) {
        UObject* o = ObjectAt(i);
        if (!o) continue;
        FName on = NameOf(o);
        if (on.Index != last.Index || on.Number != last.Number) continue;
        if (className && Name(ClassOf(o)) != className) continue;
        if (str::IEquals(PathName(o), path)) return o;
    }
    return nullptr;
}

void ForEachObject(const std::function<bool(UObject*)>& fn) {
    int32_t n = ObjectsNum();
    for (int32_t i = 0; i < n; ++i) {
        UObject* o = ObjectAt(i);
        if (o && !fn(o)) break;
    }
}

std::vector<UObject*> FindInstances(const UClass* cls, size_t maxCount) {
    std::vector<UObject*> out;
    if (!cls) return out;
    ForEachObject([&](UObject* o) {
        if (IsA(o, cls) && !IsDefaultObject(o)) out.push_back(o);
        return out.size() < maxCount;
    });
    return out;
}

UObject* FindFirstInstance(const char* className) {
    UClass* cls = FindClass(className);
    if (!cls) return nullptr;
    UObject* found = nullptr;
    ForEachObject([&](UObject* o) {
        if (IsA(o, cls) && !IsDefaultObject(o)) {
            found = o;
            return false;
        }
        return true;
    });
    return found;
}

// ---------------------------------------------------------------------------
// Fields

const std::string& PropType(const UProperty* p) { return p ? Name(ClassOf(p)) : kEmpty; }

bool IsProperty(const UField* f) {
    const std::string& t = PropType(f);
    return t.size() > 8 && t.compare(t.size() - 8, 8, "Property") == 0;
}

bool IsFunction(const UField* f) { return f && Name(ClassOf(f)) == "Function"; }

namespace {
UField* FindFieldImpl(const UStruct* s, const char* name, bool wantFunction) {
    PathKey key{s, std::string(wantFunction ? "f:" : "p:") + name};
    auto it = g_fieldCache.find(key);
    if (it != g_fieldCache.end()) return it->second;
    FName fn;
    UField* found = nullptr;
    if (FindName(name, fn)) {
        for (const UStruct* cur = s; cur && !found; cur = SuperOf(cur)) {
            int guard = 0;
            for (UField* f = ChildrenOf(cur); f && guard < 20000; f = NextOf(f), ++guard) {
                FName n = NameOf(f);
                if (n.Index == fn.Index && n.Number == fn.Number &&
                    (wantFunction ? IsFunction(f) : IsProperty(f))) {
                    found = f;
                    break;
                }
            }
        }
    }
    g_fieldCache[key] = found;
    return found;
}
}  // namespace

UProperty* FindProperty(const UStruct* s, const char* name) { return s ? FindFieldImpl(s, name, false) : nullptr; }
UFunction* FindFunction(const UStruct* s, const char* name) { return s ? FindFieldImpl(s, name, true) : nullptr; }

std::vector<UField*> Fields(const UStruct* s, bool includeSupers) {
    std::vector<UField*> out;
    for (const UStruct* cur = s; cur; cur = includeSupers ? SuperOf(cur) : nullptr) {
        int guard = 0;
        for (UField* f = ChildrenOf(cur); f && guard < 20000; f = NextOf(f), ++guard) out.push_back(f);
        if (!includeSupers) break;
    }
    return out;
}

int32_t PropOffset(const UProperty* p) { return p ? Rd<int32_t>(p, g_scan.layout.propOffset) : -1; }
int32_t PropElementSize(const UProperty* p) { return p ? Rd<int32_t>(p, g_scan.layout.propElementSize) : 0; }
int32_t PropArrayDim(const UProperty* p) { return p ? Rd<int32_t>(p, g_scan.layout.propArrayDim) : 0; }
uint64_t PropFlags(const UProperty* p) {
    return p && g_scan.layout.propFlags >= 0 ? Rd<uint64_t>(p, g_scan.layout.propFlags) : 0;
}
uint32_t PropBoolMask(const UProperty* p) { return p ? Rd<uint32_t>(p, g_scan.layout.boolBitMask) : 0; }
UStruct* PropStruct(const UProperty* p) { return p ? Rd<UStruct*>(p, g_scan.layout.structPropStruct) : nullptr; }
UClass* PropClass(const UProperty* p) { return p ? Rd<UClass*>(p, g_scan.layout.objPropClass) : nullptr; }
UProperty* PropInner(const UProperty* p) { return p ? Rd<UProperty*>(p, g_scan.layout.arrayPropInner) : nullptr; }
uint32_t FuncFlags(const UFunction* f) {
    return f && g_scan.layout.funcFlags >= 0 ? Rd<uint32_t>(f, g_scan.layout.funcFlags) : 0;
}
uint16_t FuncNative(const UFunction* f) {
    return f && g_scan.layout.funcNative >= 0 ? Rd<uint16_t>(f, g_scan.layout.funcNative) : 0;
}

// ---------------------------------------------------------------------------
// Property values

namespace {
ResolvedPath ResolvePath(const UStruct* type, const std::string& path) {
    PathKey key{type, path};
    auto it = g_pathCache.find(key);
    if (it != g_pathCache.end()) return it->second;
    ResolvedPath r;
    int32_t offset = 0;
    const UStruct* cur = type;
    std::vector<std::string> parts = str::Split(path, '.');
    for (size_t i = 0; i < parts.size(); ++i) {
        UProperty* p = FindProperty(cur, parts[i].c_str());
        if (!p) {
            r = ResolvedPath{};
            break;
        }
        offset += PropOffset(p);
        if (i + 1 < parts.size()) {
            if (PropType(p) != "StructProperty") {
                r = ResolvedPath{};
                break;
            }
            cur = PropStruct(p);
        } else {
            r.offset = offset;
            r.prop = p;
        }
    }
    g_pathCache[key] = r;
    return r;
}

bool TypeIs(const PropRef& r, const char* t) { return r && PropType(r.prop) == t; }
bool IsObjectType(const PropRef& r) {
    if (!r) return false;
    const std::string& t = PropType(r.prop);
    return t == "ObjectProperty" || t == "ClassProperty" || t == "ComponentProperty";
}
bool IsStructOf(const PropRef& r, const char* structName) {
    return TypeIs(r, "StructProperty") && Name(PropStruct(r.prop)) == structName;
}
}  // namespace

PropRef Prop(UObject* obj, const char* path) {
    PropRef r;
    if (!obj || !g_ready) return r;
    ResolvedPath rp = ResolvePath(ClassOf(obj), path);
    if (!rp.prop) return r;
    r.addr = reinterpret_cast<uint8_t*>(obj) + rp.offset;
    r.prop = rp.prop;
    return r;
}

PropRef PropInStruct(uint8_t* base, UStruct* structType, const char* path) {
    PropRef r;
    if (!base || !structType) return r;
    ResolvedPath rp = ResolvePath(structType, path);
    if (!rp.prop) return r;
    r.addr = base + rp.offset;
    r.prop = rp.prop;
    return r;
}

bool GetFloat(UObject* obj, const char* path, float& out) {
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "FloatProperty")) return false;
    std::memcpy(&out, r.addr, 4);
    return true;
}
bool SetFloat(UObject* obj, const char* path, float v) {
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "FloatProperty")) return false;
    std::memcpy(r.addr, &v, 4);
    return true;
}
bool GetInt(UObject* obj, const char* path, int32_t& out) {
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "IntProperty")) return false;
    std::memcpy(&out, r.addr, 4);
    return true;
}
bool SetInt(UObject* obj, const char* path, int32_t v) {
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "IntProperty")) return false;
    std::memcpy(r.addr, &v, 4);
    return true;
}
bool GetByte(UObject* obj, const char* path, uint8_t& out) {
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "ByteProperty")) return false;
    out = *r.addr;
    return true;
}
bool SetByte(UObject* obj, const char* path, uint8_t v) {
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "ByteProperty")) return false;
    *r.addr = v;
    return true;
}
bool GetBool(UObject* obj, const char* path, bool& out) {
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "BoolProperty")) return false;
    uint32_t bits;
    std::memcpy(&bits, r.addr, 4);
    out = (bits & PropBoolMask(r.prop)) != 0;
    return true;
}
bool SetBool(UObject* obj, const char* path, bool v) {
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "BoolProperty")) return false;
    uint32_t bits, mask = PropBoolMask(r.prop);
    std::memcpy(&bits, r.addr, 4);
    bits = v ? (bits | mask) : (bits & ~mask);
    std::memcpy(r.addr, &bits, 4);
    return true;
}
bool GetObj(UObject* obj, const char* path, UObject*& out) {
    PropRef r = Prop(obj, path);
    if (!IsObjectType(r)) return false;
    std::memcpy(&out, r.addr, P);
    return true;
}
bool SetObj(UObject* obj, const char* path, UObject* v) {
    PropRef r = Prop(obj, path);
    if (!IsObjectType(r)) return false;
    if (v && PropClass(r.prop) && PropType(r.prop) == "ObjectProperty" && !IsA(v, PropClass(r.prop))) return false;
    std::memcpy(r.addr, &v, P);
    return true;
}
bool GetName(UObject* obj, const char* path, FName& out) {
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "NameProperty")) return false;
    std::memcpy(&out, r.addr, sizeof(FName));
    return true;
}
bool SetName(UObject* obj, const char* path, const FName& v) {
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "NameProperty")) return false;
    std::memcpy(r.addr, &v, sizeof(FName));
    return true;
}
bool GetVector(UObject* obj, const char* path, FVector& out) {
    PropRef r = Prop(obj, path);
    if (!IsStructOf(r, "Vector")) return false;
    std::memcpy(&out, r.addr, sizeof(FVector));
    return true;
}
bool SetVector(UObject* obj, const char* path, const FVector& v) {
    PropRef r = Prop(obj, path);
    if (!IsStructOf(r, "Vector")) return false;
    std::memcpy(r.addr, &v, sizeof(FVector));
    return true;
}
bool GetRotator(UObject* obj, const char* path, FRotator& out) {
    PropRef r = Prop(obj, path);
    if (!IsStructOf(r, "Rotator")) return false;
    std::memcpy(&out, r.addr, sizeof(FRotator));
    return true;
}
bool SetRotator(UObject* obj, const char* path, const FRotator& v) {
    PropRef r = Prop(obj, path);
    if (!IsStructOf(r, "Rotator")) return false;
    std::memcpy(r.addr, &v, sizeof(FRotator));
    return true;
}

std::string FStringToUtf8(const FString& s) {
    if (!s.Data || s.Num <= 0) return std::string();
    size_t n = static_cast<size_t>(s.Num);
    while (n > 0 && s.Data[n - 1] == 0) --n;
    return str::Utf16ToUtf8(s.Data, n);
}

bool GetString(UObject* obj, const char* path, std::string& out) {
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "StrProperty")) return false;
    FString s;
    std::memcpy(&s, r.addr, sizeof(FString));
    out = FStringToUtf8(s);
    return true;
}

bool GetObjArray(UObject* obj, const char* path, std::vector<UObject*>& out) {
    out.clear();
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "ArrayProperty")) return false;
    const std::string& innerType = PropType(PropInner(r.prop));
    if (innerType != "ObjectProperty" && innerType != "ClassProperty" && innerType != "ComponentProperty") return false;
    TArray<UObject*> arr;
    std::memcpy(&arr, r.addr, sizeof(arr));
    if (arr.Num < 0 || arr.Num > 1000000 || (arr.Num && !arr.Data)) return false;
    out.assign(arr.Data, arr.Data + arr.Num);
    return true;
}

bool GetNameArray(UObject* obj, const char* path, std::vector<FName>& out) {
    out.clear();
    PropRef r = Prop(obj, path);
    if (!TypeIs(r, "ArrayProperty") || PropType(PropInner(r.prop)) != "NameProperty") return false;
    TArray<FName> arr;
    std::memcpy(&arr, r.addr, sizeof(arr));
    if (arr.Num < 0 || arr.Num > 1000000 || (arr.Num && !arr.Data)) return false;
    out.assign(arr.Data, arr.Data + arr.Num);
    return true;
}

std::string ValueToString(const PropRef& r) {
    if (!r) return "?";
    const std::string& t = PropType(r.prop);
    if (t == "FloatProperty") {
        float v;
        std::memcpy(&v, r.addr, 4);
        return str::Format("%g", static_cast<double>(v));
    }
    if (t == "IntProperty") {
        int32_t v;
        std::memcpy(&v, r.addr, 4);
        return std::to_string(v);
    }
    if (t == "ByteProperty") return std::to_string(*r.addr);
    if (t == "BoolProperty") {
        uint32_t bits;
        std::memcpy(&bits, r.addr, 4);
        return (bits & PropBoolMask(r.prop)) ? "True" : "False";
    }
    if (t == "NameProperty") {
        FName n;
        std::memcpy(&n, r.addr, sizeof(n));
        return NameToString(n);
    }
    if (t == "StrProperty") {
        FString s;
        std::memcpy(&s, r.addr, sizeof(s));
        return "\"" + FStringToUtf8(s) + "\"";
    }
    if (t == "ObjectProperty" || t == "ClassProperty" || t == "ComponentProperty") {
        UObject* o;
        std::memcpy(&o, r.addr, P);
        return o ? (IsValid(o) ? FullName(o) : str::Format("<invalid %p>", static_cast<void*>(o))) : "None";
    }
    if (t == "StructProperty") {
        const std::string& sn = Name(PropStruct(r.prop));
        if (sn == "Vector") {
            FVector v;
            std::memcpy(&v, r.addr, sizeof(v));
            return str::Format("(X=%.2f,Y=%.2f,Z=%.2f)", static_cast<double>(v.X), static_cast<double>(v.Y),
                               static_cast<double>(v.Z));
        }
        if (sn == "Rotator") {
            FRotator v;
            std::memcpy(&v, r.addr, sizeof(v));
            return str::Format("(Pitch=%d,Yaw=%d,Roll=%d)", v.Pitch, v.Yaw, v.Roll);
        }
        return "(" + sn + ")";
    }
    if (t == "ArrayProperty") {
        TArray<uint8_t> a;
        std::memcpy(&a, r.addr, sizeof(a));
        return str::Format("[%d elements]", a.Num);
    }
    return "<" + t + ">";
}

}  // namespace omm::ue3
