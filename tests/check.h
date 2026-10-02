// Minimal assertion helpers shared by the test files.
#pragma once

#include <cstdio>

inline int g_failures = 0;
inline int g_checks = 0;

#define CHECK(cond)                                                           \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)
