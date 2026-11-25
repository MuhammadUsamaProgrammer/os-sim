// check.h - a tiny assertion helper for the test executables.
// CHECK() records a failure (with file/line) instead of aborting, so one run
// reports every broken assertion. main() returns report().
#pragma once
#include <cmath>
#include <cstdio>
#include <string>

static int g_checks = 0, g_failures = 0;

#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b) CHECK(std::fabs((double)(a) - (double)(b)) < 1e-3)
// Checks that the expression throws the given exception type.
#define CHECK_THROWS(expr, ExType) do { ++g_checks; bool thrown = false; \
    try { expr; } catch (const ExType&) { thrown = true; } catch (...) {} \
    if (!thrown) { ++g_failures; std::printf("FAIL %s:%d: expected %s from %s\n", \
        __FILE__, __LINE__, #ExType, #expr); } } while (0)

// Prints a summary and returns the process exit code.
inline int report(const char* name) {
    std::printf("%s: %d checks, %d failures\n", name, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

// Absolute path of a file inside samples/ (works from any working directory).
inline std::string samplePath(const std::string& name) {
    return std::string(OSSIM_SOURCE_DIR) + "/samples/" + name;
}
