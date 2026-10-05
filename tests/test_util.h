// Shared check macros for the host unit tests.
#pragma once

#include <cstdio>

extern int g_failures;
extern int g_checks;

#define CHECK(cond)                                                             \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(cond)) {                                                          \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

#define CHECK_EQ(a, b)                                                                                  \
    do {                                                                                                \
        ++g_checks;                                                                                     \
        long long va_ = static_cast<long long>(a), vb_ = static_cast<long long>(b);                     \
        if (va_ != vb_) {                                                                               \
            std::printf("  FAIL %s:%d: %s == %s (0x%llX vs 0x%llX)\n", __FILE__, __LINE__, #a, #b,      \
                        static_cast<unsigned long long>(va_), static_cast<unsigned long long>(vb_));    \
            ++g_failures;                                                                               \
        }                                                                                               \
    } while (0)

