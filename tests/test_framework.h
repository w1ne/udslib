#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s at line %d\n", #cond, __LINE__); \
        return 0; \
    } \
} while(0)

#define TEST_PASS() return 1

typedef int (*test_fn_t)(void);

static void run_test(test_fn_t test, const char* name) {
    printf("[TEST] Running %s... ", name);
    if (test()) {
        printf("PASS\n");
    } else {
        printf("FAILED\n");
    }
}

#endif
