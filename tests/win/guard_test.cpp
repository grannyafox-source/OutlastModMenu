// Windows-only test for src/core/guard.cpp (runs natively or under Wine):
// real access violations inside guarded code must be recovered.
#include "../../src/core/guard.h"
#include "../../src/core/log.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
#define EXPECT(c)                                                       \
    do {                                                                \
        if (!(c)) {                                                     \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);            \
            ++g_fail;                                                   \
        }                                                               \
    } while (0)

__attribute__((noinline)) static int Deref(volatile int* p) { return *p; }
__attribute__((noinline)) static void Deep(int n, volatile int* p) {
    std::vector<int> v(64, n);  // destructor skipped on recovery (leak only)
    if (n == 0) Deref(p);
    else Deep(n - 1, p);
}

int main() {
    omm::log::Init("guard_test.log");
    omm::guard::Install();
    int value = 0;
    EXPECT(omm::guard::Run("ok", [&] { value = 42; }));
    EXPECT(value == 42);

    volatile int* bad = reinterpret_cast<volatile int*>(0x10);
    EXPECT(!omm::guard::Run("null read", [&] { value = Deref(bad); }));
    EXPECT(omm::guard::FaultCount() == 1);
    EXPECT(!omm::guard::Run("deep write", [&] { Deep(20, bad); }));
    EXPECT(!omm::guard::Run("write", [&] { *reinterpret_cast<volatile int*>(0x20) = 1; }));

    // Nested: the inner fault is caught by the inner guard only.
    bool inner = true, outerOk = false;
    outerOk = omm::guard::Run("outer", [&] {
        inner = omm::guard::Run("inner", [&] { Deref(bad); });
        value = 7;
    });
    EXPECT(outerOk && !inner && value == 7);

    // Many recoveries in a row (stack must not grow).
    int recovered = 0;
    for (int i = 0; i < 2000; ++i)
        if (!omm::guard::Run("loop", [&] { Deref(bad); })) ++recovered;
    EXPECT(recovered == 2000);
    EXPECT(omm::guard::LastFault().find("loop") != std::string::npos);

    // Still works normally afterwards.
    EXPECT(omm::guard::Run("ok again", [&] { value = 99; }) && value == 99);
    std::printf("%s (%u faults recovered)\n", g_fail ? "FAILED" : "guard test passed", omm::guard::FaultCount());
    return g_fail ? 1 : 0;
}
