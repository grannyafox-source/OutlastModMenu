#include "guard.h"

#include "common.h"
#include "log.h"
#include "strutil.h"
#include "sync.h"

#if OMM_WINDOWS
#include <intrin.h>
#endif

namespace omm::guard {

#if OMM_WINDOWS

namespace {
// One record per active Run() on a thread; linked so guards can nest.
struct Frame {
    void* jmp[5];  // __builtin_setjmp buffer
    Frame* prev;
    const char* what;
    DWORD code;
    uintptr_t address;
#if !OMM_X64
    DWORD sehHead;  // x86: SEH chain head to restore after skipping frames
#endif
};

DWORD g_tls = TLS_OUT_OF_INDEXES;
PVOID g_handler = nullptr;
volatile LONG g_faults = 0;
Mutex g_lastLock;
std::string g_last;

bool Recoverable(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_INT_OVERFLOW:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_IN_PAGE_ERROR:
            return true;
        default:
            return false;  // stack overflow, C++ exceptions, debugger events...
    }
}

// Entered (instead of the faulting instruction) with the guard's frame
// still active; jumps back into Run().
__attribute__((noreturn, noinline)) void Recover() {
    Frame* f = static_cast<Frame*>(TlsGetValue(g_tls));
    __builtin_longjmp(f->jmp, 1);
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* ep) {
    if (g_tls == TLS_OUT_OF_INDEXES || !ep || !ep->ExceptionRecord || !ep->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    Frame* f = static_cast<Frame*>(TlsGetValue(g_tls));
    if (!f) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (!Recoverable(code)) return EXCEPTION_CONTINUE_SEARCH;
    f->code = code;
    f->address = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    CONTEXT* c = ep->ContextRecord;
#if OMM_X64
    c->Rsp = (c->Rsp & ~static_cast<DWORD64>(0xF)) - 8;  // aligned as right after a call
    c->Rip = reinterpret_cast<DWORD64>(&Recover);
#else
    c->Esp = (c->Esp & ~static_cast<DWORD>(0xF)) - 4;
    c->Eip = reinterpret_cast<DWORD>(&Recover);
#endif
    return EXCEPTION_CONTINUE_EXECUTION;
}

std::string Describe(uintptr_t address) {
    HMODULE m = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &m) &&
        m) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(m, path, MAX_PATH);
        const wchar_t* name = wcsrchr(path, L'\\');
        return str::Format("%s+0x%llX", str::WideToUtf8(name ? name + 1 : path).c_str(),
                           static_cast<unsigned long long>(address - reinterpret_cast<uintptr_t>(m)));
    }
    return str::Format("%p", reinterpret_cast<void*>(address));
}

const char* CodeName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "division by zero";
        case EXCEPTION_INT_OVERFLOW: return "integer overflow";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned access";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        default: return "exception";
    }
}

// Kept out of line so the setjmp frame is simple and nothing in it lives in
// registers across the jump.
__attribute__((noinline)) bool Guarded(Frame* frame, Fn fn, void* ctx) {
    if (__builtin_setjmp(frame->jmp) == 0) {
        TlsSetValue(g_tls, frame);
        fn(ctx);
        return true;
    }
    return false;
}
}  // namespace

void Install() {
    if (g_tls == TLS_OUT_OF_INDEXES) g_tls = TlsAlloc();
    if (!g_handler && g_tls != TLS_OUT_OF_INDEXES) g_handler = AddVectoredExceptionHandler(1, &Handler);
}

bool Run(const char* what, Fn fn, void* ctx) {
    if (g_tls == TLS_OUT_OF_INDEXES || !g_handler) {
        fn(ctx);
        return true;
    }
    Frame frame{};
    frame.what = what;
    frame.prev = static_cast<Frame*>(TlsGetValue(g_tls));
#if !OMM_X64
    frame.sehHead = __readfsdword(0);
#endif
    Frame* volatile fp = &frame;
    bool ok = Guarded(fp, fn, ctx);
    if (!ok) {
#if !OMM_X64
        // The frames we skipped may have registered SEH handlers on the stack.
        __writefsdword(0, fp->sehHead);
#endif
    }
    TlsSetValue(g_tls, fp->prev);
    if (ok) return true;
    InterlockedIncrement(&g_faults);
    std::string msg = str::Format("%s: %s at %s", what ? what : "?", CodeName(fp->code), Describe(fp->address).c_str());
    LOGE("Recovered from a crash in %s", msg.c_str());
    LockGuard lock(g_lastLock);
    g_last = msg;
    return false;
}

uint32_t FaultCount() { return static_cast<uint32_t>(g_faults); }

std::string LastFault() {
    LockGuard lock(g_lastLock);
    return g_last;
}

#else  // !OMM_WINDOWS

void Install() {}
bool Run(const char*, Fn fn, void* ctx) {
    fn(ctx);
    return true;
}
uint32_t FaultCount() { return 0; }
std::string LastFault() { return std::string(); }

#endif

}  // namespace omm::guard
