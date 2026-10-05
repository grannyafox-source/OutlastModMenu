// Offsets of engine reflection structures. Everything here is discovered at
// runtime (see scanner.cpp) and validated against facts about the engine,
// so the mod survives patches and works for both the 32- and 64-bit builds.
#pragma once

#include <string>

namespace omm::ue3 {

struct Layout {
    // FNameEntry
    int nameString = -1;       // offset of the character data
    int nameIndexField = -1;   // offset of FNameEntry::Index (-1: unknown)
    bool nameIndexShifted = false;  // Index stored as (index << 1) | bIsUnicode

    // UObject
    int objIndex = -1;
    int objOuter = -1;
    int objName = -1;
    int objClass = -1;
    int objSize = -1;  // sizeof(UObject) == offset of UField::Next

    // UField / UStruct
    int fieldNext = -1;
    int structSuper = -1;
    int structChildren = -1;

    // UProperty
    int propArrayDim = -1;
    int propElementSize = -1;
    int propFlags = -1;
    int propOffset = -1;
    int propSize = -1;  // sizeof(UProperty): first member of each subclass

    // Members of UProperty subclasses (all start at propSize).
    int boolBitMask = -1;
    int objPropClass = -1;
    int classPropMeta = -1;
    int structPropStruct = -1;
    int arrayPropInner = -1;
    int bytePropEnum = -1;

    // UFunction
    int funcFlags = -1;
    int funcNative = -1;      // WORD iNative
    int funcParmsSize = -1;
    int funcRetOffset = -1;
    int funcFunc = -1;        // native thunk / UObject::ProcessInternal

    bool ObjectsReady() const { return objIndex >= 0 && objOuter >= 0 && objName >= 0 && objClass >= 0; }
    bool StructsReady() const { return fieldNext >= 0 && structSuper >= 0 && structChildren >= 0; }
    bool PropertiesReady() const { return propOffset >= 0 && propElementSize >= 0 && propArrayDim >= 0; }

    std::string Describe() const;
};

}  // namespace omm::ue3
