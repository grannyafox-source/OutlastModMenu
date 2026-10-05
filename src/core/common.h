// Outlast Mod Menu - shared platform macros and small helpers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  define OMM_WINDOWS 1
#else
#  define OMM_WINDOWS 0
#endif

#if defined(_M_X64) || defined(__x86_64__)
#  define OMM_X64 1
#else
#  define OMM_X64 0
#endif

// Calling-convention helpers. They only mean something on 32-bit x86;
// x64 Windows has a single calling convention.
#if !OMM_X64 && OMM_WINDOWS
#  define OMM_THISCALL __thiscall
#  define OMM_FASTCALL __fastcall
#else
#  define OMM_THISCALL
#  define OMM_FASTCALL
#endif

#if defined(__MINGW32__)
// Built with __USE_MINGW_ANSI_STDIO=1, so printf follows C99 (%zu, %lld...).
#  define OMM_PRINTF(fmt, args) __attribute__((format(gnu_printf, fmt, args)))
#elif defined(__GNUC__)
#  define OMM_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#  define OMM_PRINTF(fmt, args)
#endif

#define OMM_VERSION_STRING "1.0.0"
#define OMM_NAME "Outlast Mod Menu"

namespace omm {

constexpr size_t kPtrSize = sizeof(void*);

template <typename T>
inline T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Reads a value of type T from an absolute address. Callers are expected to
// have validated the address (see mem::IsReadable) when it is untrusted.
template <typename T>
inline T Read(uintptr_t addr) { T v; std::memcpy(&v, reinterpret_cast<const void*>(addr), sizeof(T)); return v; }

template <typename T>
inline void Write(uintptr_t addr, const T& v) { std::memcpy(reinterpret_cast<void*>(addr), &v, sizeof(T)); }

}  // namespace omm
