// Builds a synthetic Unreal Engine 3 object graph in ordinary heap memory so
// the scanner can be tested without the game. The layout mirrors the engine
// headers (UnObjBas.h / UnClass.h) for the pointer size of the test build,
// and can be perturbed to exercise the scanner's fallback search paths.
#pragma once

#include "../src/core/memory.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fake {

constexpr size_t P = sizeof(void*);

struct Config {
    // FNameEntry
    size_t nameString = P == 8 ? 0x18 : 0x10;
    size_t nameIndex = 8;
    // UObject
    size_t objIndex = P == 8 ? 0x38 : 0x20;
    size_t objOuter = P == 8 ? 0x40 : 0x28;
    size_t objName = P == 8 ? 0x48 : 0x2C;
    size_t objClass = P == 8 ? 0x50 : 0x34;
    size_t objSize = P == 8 ? 0x60 : 0x3C;
    // UStruct (relative to objSize + P)
    size_t structSuper() const { return objSize + P * 3; }   // after Next, ScriptText, CppText
    size_t structChildren() const { return objSize + P * 4; }
    size_t structSize() const { return objSize + P * 4 + 0x60; }
    // UProperty
    size_t propArrayDim() const { return objSize + P; }
    size_t propElementSize() const { return objSize + P + 4; }
    size_t propFlags() const { return objSize + P + 8; }
    size_t propOffset() const { return objSize + P + 8 + 8 + 2 + 2 + 8 + P + (P == 8 ? 4 : 0); }
    size_t propSize() const { return ((propOffset() + 4 + 0x18) + (P - 1)) & ~(P - 1); }
    // UFunction, relative to structSize()
    size_t funcFlags() const { return structSize(); }
    size_t funcNative() const { return structSize() + 4; }
    size_t funcParms() const { return structSize() + 18; }
    size_t funcRet() const { return structSize() + 20; }
    size_t funcFunc() const { return structSize() + (P == 8 ? 32 : 28); }
    size_t funcSize() const { return funcFunc() + P; }
};

constexpr uintptr_t kCodeBase = 0x13370000;
constexpr uintptr_t kProcessInternal = kCodeBase + 0x1000;

class World : public omm::mem::Oracle {
public:
    explicit World(const Config& c) : c_(c) {
        dataSection_.assign(0x10000, 0);
        Register(dataSection_.data(), dataSection_.size());
        // Mandatory leading names.
        for (const char* n : {"None", "ByteProperty", "IntProperty", "BoolProperty", "FloatProperty", "ObjectProperty",
                              "NameProperty", "DelegateProperty", "ClassProperty", "ArrayProperty", "StructProperty",
                              "VectorProperty", "RotatorProperty", "StrProperty", "MapProperty", "InterfaceProperty"})
            Name(n);
    }

    bool Readable(uintptr_t addr, size_t len) const override {
        auto it = blocks_.upper_bound(addr);
        if (it == blocks_.begin()) return false;
        --it;
        return addr >= it->first && addr + len <= it->first + it->second;
    }

    void Register(const void* p, size_t n) { blocks_[reinterpret_cast<uintptr_t>(p)] = n; }

    int32_t Name(const std::string& s) {
        auto it = nameIdx_.find(s);
        if (it != nameIdx_.end()) return it->second;
        size_t sz = c_.nameString + s.size() + 1;
        auto buf = std::make_unique<uint8_t[]>(sz);
        std::memset(buf.get(), 0, sz);
        int32_t idx = static_cast<int32_t>(names_.size());
        int32_t shifted = idx << 1;
        std::memcpy(buf.get() + c_.nameIndex, &shifted, 4);
        std::memcpy(buf.get() + c_.nameString, s.c_str(), s.size() + 1);
        Register(buf.get(), sz);
        names_.push_back(reinterpret_cast<uintptr_t>(buf.get()));
        storage_.push_back(std::move(buf));
        nameIdx_[s] = idx;
        return idx;
    }

    uintptr_t Alloc(size_t size) {
        auto buf = std::make_unique<uint8_t[]>(size);
        std::memset(buf.get(), 0, size);
        uintptr_t p = reinterpret_cast<uintptr_t>(buf.get());
        Register(buf.get(), size);
        storage_.push_back(std::move(buf));
        return p;
    }

    uintptr_t Object(const std::string& name, uintptr_t cls, uintptr_t outer, size_t size = 0) {
        if (!size) size = c_.objSize;
        uintptr_t o = Alloc(size);
        int32_t idx = static_cast<int32_t>(objects_.size());
        Put<int32_t>(o + c_.objIndex, idx);
        Put<uintptr_t>(o + c_.objOuter, outer);
        Put<int32_t>(o + c_.objName, Name(name));
        Put<int32_t>(o + c_.objName + 4, 0);
        Put<uintptr_t>(o + c_.objClass, cls);
        Put<uintptr_t>(o, 0xDEAD0000);  // fake vtable
        objects_.push_back(o);
        return o;
    }

    void NullSlot() { objects_.push_back(0); }

    template <typename T>
    void Put(uintptr_t addr, const T& v) { std::memcpy(reinterpret_cast<void*>(addr), &v, sizeof(T)); }
    template <typename T>
    T Get(uintptr_t addr) const { T v; std::memcpy(&v, reinterpret_cast<const void*>(addr), sizeof(T)); return v; }

    // Appends a field to a struct's Children chain (keeps declaration order).
    void AddChild(uintptr_t structObj, uintptr_t field) {
        uintptr_t first = Get<uintptr_t>(structObj + c_.structChildren());
        if (!first) {
            Put<uintptr_t>(structObj + c_.structChildren(), field);
            return;
        }
        uintptr_t cur = first;
        while (Get<uintptr_t>(cur + c_.objSize)) cur = Get<uintptr_t>(cur + c_.objSize);
        Put<uintptr_t>(cur + c_.objSize, field);
    }

    // Publishes GNames and GObjects into the fake data section at the given
    // offsets (with junk around them).
    void Publish(size_t namesOff, size_t objectsOff) {
        namesArray_ = Alloc((names_.size() + 16) * P);
        for (size_t i = 0; i < names_.size(); ++i) Put<uintptr_t>(namesArray_ + i * P, names_[i]);
        objectsArray_ = Alloc((objects_.size() + 16) * P);
        for (size_t i = 0; i < objects_.size(); ++i) Put<uintptr_t>(objectsArray_ + i * P, objects_[i]);
        uintptr_t ds = reinterpret_cast<uintptr_t>(dataSection_.data());
        // Junk: a smaller array of names and a plausible-looking array.
        Put<uintptr_t>(ds + 0x40, namesArray_);
        Put<int32_t>(ds + 0x40 + P, 3);
        Put<int32_t>(ds + 0x40 + P + 4, 3);
        Put<uintptr_t>(ds + namesOff, namesArray_);
        Put<int32_t>(ds + namesOff + P, static_cast<int32_t>(names_.size()));
        Put<int32_t>(ds + namesOff + P + 4, static_cast<int32_t>(names_.size() + 16));
        Put<uintptr_t>(ds + objectsOff, objectsArray_);
        Put<int32_t>(ds + objectsOff + P, static_cast<int32_t>(objects_.size()));
        Put<int32_t>(ds + objectsOff + P + 4, static_cast<int32_t>(objects_.size() + 16));
        namesAddr_ = ds + namesOff;
        objectsAddr_ = ds + objectsOff;
    }

    omm::mem::Range DataRange() const {
        uintptr_t b = reinterpret_cast<uintptr_t>(dataSection_.data());
        return {b, b + dataSection_.size()};
    }

    uintptr_t NamesAddr() const { return namesAddr_; }
    uintptr_t ObjectsAddr() const { return objectsAddr_; }
    const Config& Cfg() const { return c_; }
    size_t ObjectCount() const { return objects_.size(); }

private:
    Config c_;
    std::map<uintptr_t, size_t> blocks_;
    std::vector<std::unique_ptr<uint8_t[]>> storage_;
    std::vector<uint8_t> dataSection_;
    std::vector<uintptr_t> names_;
    std::map<std::string, int32_t> nameIdx_;
    std::vector<uintptr_t> objects_;
    uintptr_t namesArray_ = 0, objectsArray_ = 0, namesAddr_ = 0, objectsAddr_ = 0;
};

// Populates a world with the classes, properties and functions the scanner
// relies on, plus thousands of filler objects.
struct Graph {
    uintptr_t core = 0, engine = 0;
    uintptr_t classClass = 0, objectClass = 0, packageClass = 0, functionClass = 0, scriptStructClass = 0,
              enumClass = 0;
    uintptr_t actor = 0, pawn = 0, controller = 0, playerController = 0, cheatManager = 0;
    uintptr_t vectorStruct = 0, rotatorStruct = 0, physicsEnum = 0;
    uintptr_t location = 0, setLocation = 0;
    uintptr_t modifiersStruct = 0;
    uintptr_t actorInstance = 0, pawnInstance = 0;
    int32_t locationOffset = 0;
    std::map<std::string, uintptr_t> propClasses;
};

inline Graph Build(World& w) {
    const Config& c = w.Cfg();
    Graph g;
    // Class objects are created with a temporary null class, then patched.
    g.classClass = w.Object("Class", 0, 0, c.structSize() + 0x40);
    g.core = w.Object("Core", 0, 0);
    w.Put<uintptr_t>(g.classClass + c.objOuter, g.core);
    w.Put<uintptr_t>(g.classClass + c.objClass, g.classClass);
    g.packageClass = w.Object("Package", g.classClass, g.core, c.structSize() + 0x40);
    w.Put<uintptr_t>(g.core + c.objClass, g.packageClass);
    g.engine = w.Object("Engine", g.packageClass, 0);
    g.objectClass = w.Object("Object", g.classClass, g.core, c.structSize() + 0x40);
    g.functionClass = w.Object("Function", g.classClass, g.core, c.structSize() + 0x40);
    g.scriptStructClass = w.Object("ScriptStruct", g.classClass, g.core, c.structSize() + 0x40);
    g.enumClass = w.Object("Enum", g.classClass, g.core, c.structSize() + 0x40);
    for (const char* pc : {"ByteProperty", "IntProperty", "BoolProperty", "FloatProperty", "ObjectProperty",
                           "ClassProperty", "StructProperty", "ArrayProperty", "NameProperty", "StrProperty"})
        g.propClasses[pc] = w.Object(pc, g.classClass, g.core, c.structSize() + 0x40);

    auto cls = [&](const char* name, uintptr_t super, uintptr_t pkg) {
        uintptr_t o = w.Object(name, g.classClass, pkg, c.structSize() + 0x40);
        w.Put<uintptr_t>(o + c.structSuper(), super);
        return o;
    };
    g.actor = cls("Actor", g.objectClass, g.engine);
    g.pawn = cls("Pawn", g.actor, g.engine);
    g.controller = cls("Controller", g.actor, g.engine);
    g.playerController = cls("PlayerController", g.controller, g.engine);
    g.cheatManager = cls("CheatManager", g.objectClass, g.engine);
    g.vectorStruct = w.Object("Vector", g.scriptStructClass, g.objectClass, c.structSize());
    g.rotatorStruct = w.Object("Rotator", g.scriptStructClass, g.objectClass, c.structSize());
    g.physicsEnum = w.Object("EPhysics", g.enumClass, g.actor, c.objSize + 0x20);

    auto prop = [&](uintptr_t owner, const char* name, const char* type, int32_t offset, int32_t elemSize,
                    uint64_t flags, uintptr_t extra, uintptr_t extra2 = 0) {
        uintptr_t p = w.Object(name, g.propClasses[type], owner, c.propSize() + 2 * P);
        w.Put<int32_t>(p + c.propArrayDim(), 1);
        w.Put<int32_t>(p + c.propElementSize(), elemSize);
        w.Put<uint64_t>(p + c.propFlags(), flags);
        w.Put<int32_t>(p + c.propOffset(), offset);
        w.Put<uintptr_t>(p + c.propSize(), extra);
        w.Put<uintptr_t>(p + c.propSize() + P, extra2);
        w.AddChild(owner, p);
        return p;
    };
    const int32_t base = static_cast<int32_t>(P == 8 ? 0x1D0 : 0x110);
    uintptr_t inner = w.Object("Components", g.propClasses["ObjectProperty"], g.actor, c.propSize() + 2 * P);
    prop(g.actor, "Components", "ArrayProperty", base, static_cast<int32_t>(P + 8), 0x1, inner);
    prop(g.actor, "AllComponents", "ArrayProperty", base + static_cast<int32_t>(P + 8), static_cast<int32_t>(P + 8), 0x1,
         inner);
    int32_t locOff = base + static_cast<int32_t>(2 * (P + 8));
    g.location = prop(g.actor, "Location", "StructProperty", locOff, 12, 0x1, g.vectorStruct);
    prop(g.actor, "Rotation", "StructProperty", locOff + 12, 12, 0x1, g.rotatorStruct);
    prop(g.actor, "DrawScale", "FloatProperty", locOff + 24, 4, 0x1, 0);
    prop(g.actor, "DrawScale3D", "StructProperty", locOff + 28, 12, 0x1, g.vectorStruct);
    prop(g.actor, "PrePivot", "StructProperty", locOff + 40, 12, 0x1, g.vectorStruct);
    prop(g.actor, "Owner", "ObjectProperty", locOff + 52, static_cast<int32_t>(P), 0x0, g.actor);
    prop(g.actor, "Physics", "ByteProperty", locOff + 52 + static_cast<int32_t>(P), 1, 0x0, g.physicsEnum);
    prop(g.actor, "bHidden", "BoolProperty", locOff + 56 + static_cast<int32_t>(P), 4, 0x0, 1);
    prop(g.actor, "bStatic", "BoolProperty", locOff + 56 + static_cast<int32_t>(P), 4, 0x0, 2);
    prop(g.pawn, "Controller", "ObjectProperty", 0x300, static_cast<int32_t>(P), 0, g.controller);
    g.modifiersStruct = w.Object("EnemyModifiers", g.scriptStructClass, g.pawn, c.structSize());
    prop(g.modifiersStruct, "bUseKillingBlow", "BoolProperty", 0, 4, 0, 1);
    prop(g.modifiersStruct, "bShouldAttack", "BoolProperty", 0, 4, 0, 2);
    prop(g.modifiersStruct, "WeaponToUse", "ByteProperty", 4, 1, 0, 0);
    prop(g.pawn, "Modifiers", "StructProperty", 0x320, 8, 0, g.modifiersStruct);
    prop(g.pawn, "Health", "IntProperty", 0x330, 4, 0, 0);
    prop(g.pawn, "GroundSpeed", "FloatProperty", 0x334, 4, 0, 0);
    prop(g.controller, "Pawn", "ObjectProperty", 0x300, static_cast<int32_t>(P), 0, g.pawn);
    prop(g.playerController, "CheatClass", "ClassProperty", 0x400, static_cast<int32_t>(P), 0, g.classClass,
         g.cheatManager);

    uintptr_t nativeThunk = kCodeBase + 0x5000;
    auto func = [&](uintptr_t owner, const char* name, uint32_t flags, uint16_t iNative, uintptr_t thunk) {
        uintptr_t f = w.Object(name, g.functionClass, owner, c.funcSize() + 0x10);
        w.Put<uint32_t>(f + c.funcFlags(), flags);
        w.Put<uint16_t>(f + c.funcNative(), iNative);
        w.Put<uintptr_t>(f + c.funcFunc(), thunk);
        w.AddChild(owner, f);
        return f;
    };
    g.setLocation = func(g.actor, "SetLocation", 0x403, 267, nativeThunk += 0x40);
    prop(g.setLocation, "NewLocation", "StructProperty", 0, 12, 0x80, g.vectorStruct);
    prop(g.setLocation, "ReturnValue", "BoolProperty", 12, 4, 0x480, 1);
    w.Put<uint16_t>(g.setLocation + c.funcParms(), 16);
    w.Put<uint16_t>(g.setLocation + c.funcRet(), 12);
    func(g.actor, "Destroy", 0x403, 279, nativeThunk += 0x40);
    func(g.actor, "Spawn", 0x403, 0, nativeThunk += 0x40);
    func(g.actor, "PostBeginPlay", 0x802, 0, kProcessInternal);
    for (int i = 0; i < 400; ++i)
        func(i % 2 ? g.pawn : g.controller, ("ScriptFunc" + std::to_string(i)).c_str(), 0x2, 0, kProcessInternal);
    for (int i = 0; i < 120; ++i) func(g.playerController, ("NativeFunc" + std::to_string(i)).c_str(), 0x403, 0,
                                       nativeThunk += 0x40);

    g.locationOffset = locOff;
    g.actorInstance = w.Object("Actor_7", g.actor, g.engine, 0x600);
    g.pawnInstance = w.Object("Pawn_0", g.pawn, g.engine, 0x600);
    w.Object("Default__Pawn", g.pawn, g.engine, 0x600);
    w.Put<float>(g.actorInstance + locOff, 1.5f);
    w.Put<float>(g.actorInstance + locOff + 4, -2.0f);
    w.Put<float>(g.actorInstance + locOff + 8, 300.0f);
    w.Put<uintptr_t>(g.actorInstance + locOff + 52, g.pawnInstance);  // Owner
    w.Put<uint32_t>(g.actorInstance + locOff + 56 + static_cast<int32_t>(P), 2);  // bStatic set, bHidden clear
    w.Put<int32_t>(g.pawnInstance + 0x330, 100);

    // Filler objects so the tables look like a real game (> 10k objects).
    for (int i = 0; i < 12000; ++i) {
        if (i % 97 == 0) w.NullSlot();
        w.Object("Filler" + std::to_string(i % 3000), i % 5 ? g.objectClass : g.cheatManager, i % 7 ? g.engine : g.core);
    }
    return g;
}

}  // namespace fake
