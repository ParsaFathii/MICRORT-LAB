/* harness.h — tiny deterministic assert-based test harness (task 2-a).
 *
 * Design: failures are COUNTED, never abort — every check runs so one
 * report shows all failures. Output goes to stdout with a fixed format
 * (no timestamps, no randomness). main() returns TEST_SUMMARY() which is
 * non-zero exactly when at least one assertion failed.
 *
 * Usage per test binary:
 *   #define TH_SUITE "test_task"        (optional; defaults below)
 *   #include "harness.h"
 *   ...
 *   TEST_NAME("section");               (starts a logical test)
 *   TEST_ASSERT(cond);                  (boolean)
 *   TEST_ASSERT_EQ_INT(a, b);           (compares as long long)
 *   TEST_ASSERT_EQ_U64(a, b);           (compares as unsigned long long)
 *   TEST_ASSERT_EQ_SIZE(a, b);          (compares as size_t)
 *   ...
 *   return TEST_SUMMARY();
 *
 * Copyright © 2026 Parsa Fathi. Apache-2.0.
 */
#ifndef MICRORT_KERNEL_TEST_HARNESS_H
#define MICRORT_KERNEL_TEST_HARNESS_H

#include <stdio.h>

#ifndef TH_SUITE
#define TH_SUITE "kernel-tests"
#endif

static int th_checks = 0;
static int th_failures = 0;
static int th_tests = 0;
static const char *th_name = "(unset)";

#define TEST_NAME(name) do { th_name = (name); th_tests++; } while (0)

#define TEST_ASSERT(cond)                                                    \
    do {                                                                     \
        th_checks++;                                                         \
        if (!(cond)) {                                                       \
            th_failures++;                                                   \
            printf("FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, th_name,      \
                   #cond);                                                   \
        }                                                                    \
    } while (0)

#define TEST_ASSERT_EQ_INT(a, b)                                             \
    do {                                                                     \
        long long th_a = (long long)(a), th_b = (long long)(b);              \
        th_checks++;                                                         \
        if (th_a != th_b) {                                                  \
            th_failures++;                                                   \
            printf("FAIL %s:%d [%s] %s == %s (got %lld, want %lld)\n",       \
                   __FILE__, __LINE__, th_name, #a, #b, th_a, th_b);         \
        }                                                                    \
    } while (0)

#define TEST_ASSERT_EQ_U64(a, b)                                             \
    do {                                                                     \
        unsigned long long th_a = (unsigned long long)(a);                   \
        unsigned long long th_b = (unsigned long long)(b);                   \
        th_checks++;                                                         \
        if (th_a != th_b) {                                                  \
            th_failures++;                                                   \
            printf("FAIL %s:%d [%s] %s == %s (got %llu, want %llu)\n",       \
                   __FILE__, __LINE__, th_name, #a, #b, th_a, th_b);         \
        }                                                                    \
    } while (0)

#define TEST_ASSERT_EQ_SIZE(a, b)                                            \
    do {                                                                     \
        size_t th_a = (size_t)(a), th_b = (size_t)(b);                       \
        th_checks++;                                                         \
        if (th_a != th_b) {                                                  \
            th_failures++;                                                   \
            printf("FAIL %s:%d [%s] %s == %s (got %zu, want %zu)\n",         \
                   __FILE__, __LINE__, th_name, #a, #b, th_a, th_b);         \
        }                                                                    \
    } while (0)

/* Prints the summary line; evaluates to the process exit status. */
#define TEST_SUMMARY()                                                       \
    (printf("---- %s: %d checks, %d tests, %d failures => %s\n",             \
            TH_SUITE, th_checks, th_tests, th_failures,                      \
            (th_failures == 0) ? "PASS" : "FAIL"),                           \
     (th_failures != 0))

#endif /* MICRORT_KERNEL_TEST_HARNESS_H */
