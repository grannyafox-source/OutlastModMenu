// Minimal mutex that does not depend on the libstdc++ thread model, so the
// DLL builds the same with MSVC and with MinGW's "win32" threading variant.
#pragma once

#include "common.h"

#if !OMM_WINDOWS
#include <mutex>
#endif

namespace omm {

class Mutex {
public:
    Mutex() {
#if OMM_WINDOWS
        InitializeSRWLock(&lock_);
#endif
    }
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void Lock() {
#if OMM_WINDOWS
        AcquireSRWLockExclusive(&lock_);
#else
        m_.lock();
#endif
    }
    void Unlock() {
#if OMM_WINDOWS
        ReleaseSRWLockExclusive(&lock_);
#else
        m_.unlock();
#endif
    }

private:
#if OMM_WINDOWS
    SRWLOCK lock_;
#else
    std::mutex m_;
#endif
};

class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.Lock(); }
    ~LockGuard() { m_.Unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Mutex& m_;
};

}  // namespace omm
