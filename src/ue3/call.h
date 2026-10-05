// Calling UnrealScript / native functions through UObject::ProcessEvent.
//
// ProcessEvent is a virtual function whose vtable slot differs between
// builds. It is discovered at runtime from inside our frame hook: the hooked
// UFunction is invoked *by* ProcessEvent, so the hook's return address lies
// inside it (see DiscoverProcessEvent).
#pragma once

#include "engine.h"

#include <memory>
#include <string>
#include <vector>

namespace omm::ue3 {

void SetGameThread(uint32_t threadId);
bool OnGameThread();

bool ProcessEventReady();
int ProcessEventIndex();
uintptr_t ProcessEventAddress();
void DiscoverProcessEvent(UObject* self, uintptr_t returnAddress);

// Raw call. Natives with a fixed native index (e.g. Actor.SetLocation = 267)
// are rejected by ProcessEvent, so their iNative is cleared for the call.
bool ProcessEvent(UObject* obj, UFunction* fn, void* params);

// Builds the parameter block for one call by looking up each parameter by
// name, so it adapts to the actual function signature in the game.
class Call {
public:
    Call(UObject* obj, const char* function);
    Call(UObject* obj, UFunction* fn);

    bool Ok() const { return obj_ && fn_; }
    UFunction* Function() const { return fn_; }

    Call& Float(const char* param, float v);
    Call& Int(const char* param, int32_t v);
    Call& Byte(const char* param, uint8_t v);
    Call& Bool(const char* param, bool v);
    Call& Obj(const char* param, UObject* v);
    Call& NameP(const char* param, const FName& v);
    Call& Str(const char* param, const std::string& utf8);
    Call& Vector(const char* param, const FVector& v);
    Call& Rotator(const char* param, const FRotator& v);
    Call& Raw(const char* param, const void* data, size_t size);

    // Returns false if the function is missing, a parameter had the wrong
    // type, or ProcessEvent is not available yet.
    bool Invoke();

    float RetFloat() { return OutFloat("ReturnValue"); }
    int32_t RetInt() { return OutInt("ReturnValue"); }
    bool RetBool() { return OutBool("ReturnValue"); }
    UObject* RetObj() { return OutObj("ReturnValue"); }
    std::string RetStr() { return OutStr("ReturnValue"); }

    float OutFloat(const char* param);
    int32_t OutInt(const char* param);
    bool OutBool(const char* param);
    UObject* OutObj(const char* param);
    std::string OutStr(const char* param);
    FVector OutVector(const char* param);
    FRotator OutRotator(const char* param);

    bool HasParam(const char* param) const;
    const std::string& Error() const { return error_; }

private:
    void Prepare();
    uint8_t* Param(const char* name, const char* type, UProperty** outProp = nullptr);

    UObject* obj_ = nullptr;
    UFunction* fn_ = nullptr;
    std::vector<uint8_t> buf_;
    std::vector<std::unique_ptr<std::u16string>> strings_;
    std::string error_;
};

// Calls a parameterless function by name. Returns false when missing.
bool CallNoArgs(UObject* obj, const char* function);

}  // namespace omm::ue3
