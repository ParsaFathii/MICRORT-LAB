// test_harness.hpp — tiny deterministic C++ test harness (engine tests).
//
// Mirrors kernel/tests/harness.h: failures are COUNTED, never abort — every
// check runs so a single report shows all problems. Output is printf-style
// to stdout with a fixed format (no timestamps, no randomness). main()
// returns TEST_SUMMARY(), non-zero exactly when at least one check failed.
//
// Usage (one .cpp per test binary — the static counters below rely on it):
//   #include "test_harness.hpp"
//   TEST("section name");          // starts a logical test
//   CHECK(cond);                   // boolean
//   CHECK_TRUE(cond);              // alias of CHECK
//   CHECK_EQ(a, b);                // operator== + stream-printed values
//   CHECK_NE(a, b);                 // operator!=
//   CHECK_STREQ(a, b);             // C-string / std::string equality
//   return TEST_SUMMARY();
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#ifndef MICRORT_ENGINE_TEST_HARNESS_HPP
#define MICRORT_ENGINE_TEST_HARNESS_HPP

#include <cstdio>
#include <sstream>
#include <string>

#ifndef TH_SUITE
#define TH_SUITE "engine-tests"
#endif

static int th_checks = 0;
static int th_failures = 0;
static int th_tests = 0;
static const char* th_name = "(unset)";

#define TEST(name) do { th_name = (name); ++th_tests; } while (0)

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++th_checks;                                                           \
        if (!(cond)) {                                                         \
            ++th_failures;                                                     \
            std::printf("FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, th_name,   \
                        #cond);                                                \
        }                                                                      \
    } while (0)

#define CHECK_TRUE(cond) CHECK(cond)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        ++th_checks;                                                           \
        const auto& th_a = (a);                                                \
        const auto& th_b = (b);                                                \
        if (!(th_a == th_b)) {                                                 \
            ++th_failures;                                                     \
            std::ostringstream th_sa, th_sb;                                   \
            th_sa << th_a;                                                     \
            th_sb << th_b;                                                     \
            std::printf("FAIL %s:%d [%s] %s == %s (got %s, want %s)\n",        \
                        __FILE__, __LINE__, th_name, #a, #b,                   \
                        th_sa.str().c_str(), th_sb.str().c_str());             \
        }                                                                      \
    } while (0)

#define CHECK_NE(a, b)                                                         \
    do {                                                                       \
        ++th_checks;                                                           \
        const auto& th_a = (a);                                                \
        const auto& th_b = (b);                                                \
        if (th_a == th_b) {                                                    \
            ++th_failures;                                                     \
            std::ostringstream th_sa, th_sb;                                   \
            th_sa << th_a;                                                     \
            th_sb << th_b;                                                     \
            std::printf("FAIL %s:%d [%s] %s != %s (both %s)\n", __FILE__,      \
                        __LINE__, th_name, #a, #b, th_sa.str().c_str());       \
        }                                                                      \
    } while (0)

#define CHECK_STREQ(a, b)                                                      \
    do {                                                                       \
        ++th_checks;                                                           \
        const std::string th_a = (a);                                          \
        const std::string th_b = (b);                                          \
        if (th_a != th_b) {                                                    \
            ++th_failures;                                                     \
            std::printf("FAIL %s:%d [%s] '%s' == '%s'\n", __FILE__, __LINE__,  \
                        th_name, th_a.c_str(), th_b.c_str());                  \
        }                                                                      \
    } while (0)

// Prints the summary line; evaluates to the process exit status.
#define TEST_SUMMARY()                                                         \
    (std::printf("---- %s: %d checks, %d tests, %d failures => %s\n",          \
                 TH_SUITE, th_checks, th_tests, th_failures,                   \
                 (th_failures == 0) ? "PASS" : "FAIL"),                        \
     (th_failures != 0))

#endif // MICRORT_ENGINE_TEST_HARNESS_HPP
