#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>

static int g_fail, g_pass;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (cond) {                                                                  \
            g_pass++;                                                                \
        } else {                                                                     \
            g_fail++;                                                                \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                                               \
    do {                                                                             \
        unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
        if (_a == _b) {                                                              \
            g_pass++;                                                                \
        } else {                                                                     \
            g_fail++;                                                                \
            fprintf(stderr, "  FAIL %s:%d: %s == %s (0x%llx != 0x%llx)\n", __FILE__, __LINE__, #a, #b, _a, _b); \
        }                                                                            \
    } while (0)

#define TEST_DONE()                                                                  \
    do {                                                                             \
        printf("%s: %d passed, %d failed\n", __FILE__, g_pass, g_fail);              \
        return g_fail ? 1 : 0;                                                       \
    } while (0)

#endif
