// Hooks UnrealScript functions by swapping UFunction::Func.
//
// For script functions Func points at UObject::ProcessInternal and
// ProcessEvent invokes it as "(this->*Function->Func)(Stack, Result)", so
// replacing the pointer on one UFunction intercepts every native->script
// call of that event (e.g. GameViewportClient.PostRender, once per frame)
// without patching any engine code.
#pragma once

#include "engine.h"

namespace omm::ue3 {

using HookCallback = void (*)(UObject* self);

// Runs `after` on the game thread after the original function returns.
bool HookFunction(UFunction* fn, HookCallback after);
bool IsHooked(UFunction* fn);
void UnhookAll();

// Fallback when Func swapping does not fire: detour UObject::ProcessInternal
// and filter on FFrame::Node. Requires the address found by the scanner.
bool InstallProcessInternalHook(UFunction* target, HookCallback after);
bool ProcessInternalHookActive();

// Count of hook invocations (diagnostics / watchdog).
uint64_t HookCalls();

}  // namespace omm::ue3
