// Locates the engine's global name/object tables and works out the memory
// layout of the reflection structures without any hard-coded addresses.
//
// Every stage relies on invariants of Unreal Engine 3 rather than on a
// particular build:
//   * FName table: entry 0 is "None", entry 1 "ByteProperty", 2 "IntProperty".
//   * Object table: every object stores its own index in the table.
//   * Class of a class is "Class", whose class is itself.
//   * Actor declares Location, Rotation (+12), DrawScale (+12), DrawScale3D
//     (+4), PrePivot (+12) back to back.
//   * Actor.SetLocation has native index 267 and Destroy 279.
// The scanner only talks to memory through mem::Oracle so it can be unit
// tested against a synthetic object graph.
#pragma once

#include "../core/memory.h"
#include "layout.h"

#include <functional>
#include <string>
#include <vector>

namespace omm::ue3 {

struct ScanResult {
    uintptr_t gnames = 0;    // address of TArray<FNameEntry*>
    uintptr_t gobjects = 0;  // address of TArray<UObject*>
    uintptr_t processInternal = 0;
    Layout layout;
    std::vector<std::string> notes;
};

class Scanner {
public:
    Scanner(const mem::Oracle& oracle, std::vector<mem::Range> dataRanges, std::function<bool(uintptr_t)> isCode);

    bool FindNames();
    bool FindObjects();
    bool DetectObjectLayout();
    bool DetectStructLayout();
    bool DetectPropertyLayout();
    bool DetectFunctionLayout();

    // Runs every stage. Function-layout problems are not fatal (the mod
    // falls back to script wrappers), everything else is.
    bool RunAll();

    const ScanResult& Result() const { return r_; }

private:
    bool ReadPtr(uintptr_t addr, uintptr_t& out) const;
    bool ReadI32(uintptr_t addr, int32_t& out) const;
    bool ReadU16(uintptr_t addr, uint16_t& out) const;
    bool ReadU32(uintptr_t addr, uint32_t& out) const;
    bool ReadU64(uintptr_t addr, uint64_t& out) const;
    bool MatchStr(uintptr_t addr, const char* s) const;

    int32_t NamesNum() const;
    uintptr_t NameEntry(int32_t idx) const;
    std::string NameAt(int32_t idx) const;
    int32_t FindNameIndex(const char* s) const;

    int32_t ObjectsNum() const;
    uintptr_t ObjectAt(int32_t i) const;
    bool IsObject(uintptr_t p) const;
    std::string ObjName(uintptr_t obj) const;
    int32_t ObjNameIndex(uintptr_t obj) const;
    uintptr_t ClassOf(uintptr_t obj) const;
    uintptr_t OuterOf(uintptr_t obj) const;
    uintptr_t FindObject(const char* name, const char* className, const char* outerName) const;
    uintptr_t FindChild(uintptr_t structObj, const char* name) const;
    std::vector<uintptr_t> ChildrenOf(uintptr_t structObj) const;

    void Note(const char* fmt, ...) OMM_PRINTF(2, 3);

    const mem::Oracle& oracle_;
    std::vector<mem::Range> ranges_;
    std::function<bool(uintptr_t)> isCode_;
    ScanResult r_;
    std::vector<uintptr_t> sample_;

    // Well-known objects resolved during layout detection.
    uintptr_t objectClass_ = 0, classClass_ = 0, actorClass_ = 0, pawnClass_ = 0, controllerClass_ = 0;
};

}  // namespace omm::ue3
