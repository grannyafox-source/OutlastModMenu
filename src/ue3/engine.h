// Runtime access to Unreal Engine 3 reflection data (names, objects,
// classes, properties). Built on the layout discovered by Scanner.
//
// Threading: everything here must be used from the game thread, except
// for the read-only snapshots the mod copies out for the UI.
#pragma once

#include "layout.h"
#include "scanner.h"
#include "types.h"

#include <functional>
#include <string>
#include <vector>

namespace omm::ue3 {

// ---------------------------------------------------------------------------
// Lifecycle

bool Init(const ScanResult& scan);
bool Ready();
const Layout& L();
const ScanResult& Scan();

// ---------------------------------------------------------------------------
// Names

int32_t NamesNum();
const std::string& NameString(int32_t index);   // cached, without number suffix
std::string NameToString(const FName& n);       // "Foo" or "Foo_3"
bool FindName(const std::string& s, FName& out);  // only names that already exist

// ---------------------------------------------------------------------------
// Objects

int32_t ObjectsNum();
UObject* ObjectAt(int32_t index);
bool IsValid(const UObject* o);            // pointer is a live entry of GObjects
int32_t IndexOf(const UObject* o);
UClass* ClassOf(const UObject* o);
UObject* OuterOf(const UObject* o);
FName NameOf(const UObject* o);
const std::string& Name(const UObject* o);  // short name (cached)
std::string PathName(const UObject* o);     // Outer.Outer.Name
std::string FullName(const UObject* o);     // "Class Outer.Name"
bool IsDefaultObject(const UObject* o);     // "Default__..." archetypes
bool IsA(const UObject* o, const UClass* cls);
bool IsA(const UObject* o, const char* className);

UStruct* SuperOf(const UStruct* s);
UField* ChildrenOf(const UStruct* s);
UField* NextOf(const UField* f);
bool IsChildOf(const UStruct* s, const UStruct* parent);

UClass* ClassClass();
// Class by short name, e.g. "OLHero". Misses are re-checked at most every
// couple of seconds because classes appear as packages load.
UClass* FindClass(const char* name);
// Any object by short name (and optionally its class's short name).
UObject* FindObject(const char* name, const char* className = nullptr);
// Object by dotted path name ("Package.Group.Name").
UObject* FindObjectByPath(const std::string& path, const char* className = nullptr);

// Visits every live object. Return false from fn to stop.
void ForEachObject(const std::function<bool(UObject*)>& fn);
std::vector<UObject*> FindInstances(const UClass* cls, size_t maxCount = 4096);
// First non-default instance of a class (slow: scans GObjects).
UObject* FindFirstInstance(const char* className);

// ---------------------------------------------------------------------------
// Fields

UProperty* FindProperty(const UStruct* s, const char* name);  // walks super structs
UFunction* FindFunction(const UStruct* s, const char* name);
std::vector<UField*> Fields(const UStruct* s, bool includeSupers);
bool IsProperty(const UField* f);
bool IsFunction(const UField* f);

const std::string& PropType(const UProperty* p);  // e.g. "FloatProperty"
int32_t PropOffset(const UProperty* p);
int32_t PropElementSize(const UProperty* p);
int32_t PropArrayDim(const UProperty* p);
uint64_t PropFlags(const UProperty* p);
uint32_t PropBoolMask(const UProperty* p);
UStruct* PropStruct(const UProperty* p);   // StructProperty
UClass* PropClass(const UProperty* p);     // ObjectProperty / ClassProperty
UProperty* PropInner(const UProperty* p);  // ArrayProperty
uint32_t FuncFlags(const UFunction* f);
uint16_t FuncNative(const UFunction* f);

// ---------------------------------------------------------------------------
// Property values. Paths may name struct members: "Modifiers.bShouldAttack".

struct PropRef {
    uint8_t* addr = nullptr;
    UProperty* prop = nullptr;
    explicit operator bool() const { return addr && prop; }
};

PropRef Prop(UObject* obj, const char* path);
PropRef PropInStruct(uint8_t* base, UStruct* structType, const char* path);

bool GetFloat(UObject* obj, const char* path, float& out);
bool SetFloat(UObject* obj, const char* path, float v);
bool GetInt(UObject* obj, const char* path, int32_t& out);
bool SetInt(UObject* obj, const char* path, int32_t v);
bool GetByte(UObject* obj, const char* path, uint8_t& out);
bool SetByte(UObject* obj, const char* path, uint8_t v);
bool GetBool(UObject* obj, const char* path, bool& out);
bool SetBool(UObject* obj, const char* path, bool v);
bool GetObj(UObject* obj, const char* path, UObject*& out);
bool SetObj(UObject* obj, const char* path, UObject* v);
bool GetName(UObject* obj, const char* path, FName& out);
bool SetName(UObject* obj, const char* path, const FName& v);
bool GetVector(UObject* obj, const char* path, FVector& out);
bool SetVector(UObject* obj, const char* path, const FVector& v);
bool GetRotator(UObject* obj, const char* path, FRotator& out);
bool SetRotator(UObject* obj, const char* path, const FRotator& v);
bool GetString(UObject* obj, const char* path, std::string& out);
bool GetObjArray(UObject* obj, const char* path, std::vector<UObject*>& out);
bool GetNameArray(UObject* obj, const char* path, std::vector<FName>& out);
// Reads a generic property as text (for diagnostics / the property editor).
std::string ValueToString(const PropRef& r);

// Convenience getters returning a default when the property is missing.
inline UObject* Obj(UObject* o, const char* path) {
    UObject* v = nullptr;
    return o && GetObj(o, path, v) ? v : nullptr;
}
inline float Float(UObject* o, const char* path, float def = 0.f) {
    float v = def;
    return o && GetFloat(o, path, v) ? v : def;
}
inline bool Bool(UObject* o, const char* path, bool def = false) {
    bool v = def;
    return o && GetBool(o, path, v) ? v : def;
}
inline int32_t Int(UObject* o, const char* path, int32_t def = 0) {
    int32_t v = def;
    return o && GetInt(o, path, v) ? v : def;
}

std::string FStringToUtf8(const FString& s);

}  // namespace omm::ue3
