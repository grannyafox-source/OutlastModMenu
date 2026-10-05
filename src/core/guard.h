// Crash guard for code that reads or writes game memory.
//
// The mod works from layouts it discovers at runtime, so a wrong guess or an
// object freed by the engine at the wrong moment must not take the game down.
// guard::Run executes a function; if a CPU exception (access violation,
// illegal instruction, division by zero...) happens inside it on the same
// thread, execution resumes after the call, the fault is logged and Run
// returns false.
//
// Destructors of objects created inside the guarded function do not run when
// a fault is recovered, so guarded code must not hold locks while it touches
// game memory.
#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

namespace omm::guard {

void Install();  // registers the exception handler (idempotent)

using Fn = void (*)(void* ctx);
bool Run(const char* what, Fn fn, void* ctx);

template <typename F>
bool Run(const char* what, F&& f) {
    return Run(what, [](void* p) { (*static_cast<std::remove_reference_t<F>*>(p))(); }, &f);
}

uint32_t FaultCount();
std::string LastFault();  // "what: exception at module+offset"

}  // namespace omm::guard
