#pragma once

#include <cstdio>
#include <cstdlib>

// The check-and-count harness every test executable in this repo runs on.
//
// These are console executables rather than a test framework, because the mod
// links against the game's own runtime and a framework would be a second one.
// Keeping the counter, the failure format and the summary in one place is what
// stops the suites from drifting into reporting failures differently.
namespace metroex_test {

inline int g_failures = 0;

inline void Check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// Written as a range test rather than a subtraction so a NaN fails it instead of
// passing through whichever comparison happens to be first.
inline void CheckNear(float actual, float expected, float tolerance, const char* what) {
    if (actual >= expected - tolerance && actual <= expected + tolerance) return;
    std::printf("FAIL: %s (expected %.6f, got %.6f)\n", what, static_cast<double>(expected),
                static_cast<double>(actual));
    ++g_failures;
}

// The exit code for main(). Non-zero is what ctest reads as a failed suite.
inline int Report() {
    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("all checks passed\n");
    return EXIT_SUCCESS;
}

}  // namespace metroex_test
