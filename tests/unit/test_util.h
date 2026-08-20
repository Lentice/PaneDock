#pragma once

// Shared assertion helper for PaneDock unit tests.
//
// There is no test framework. Each test file is one executable with its own
// main(), registered in tests/CMakeLists.txt. A failing EXPECT prints the
// location and the expression, then exits non-zero so ctest reports it.
//
// A test asserts externally observable behavior through a public boundary.
// It states an input and an expected output. It does not assert on internal
// call sequences, private structure, or the identity of collaborating objects.

#include <cstdio>
#include <cstdlib>

namespace panedock::test {

inline int g_failures = 0;

inline void expect(bool condition, const char* expression,
                   const char* file, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAILED %s:%d: %s\n", file, line, expression);
        ++g_failures;
    }
}

// Returns the process exit code: 0 when every expectation held.
inline int summary(const char* test_name) {
    if (g_failures == 0) {
        std::printf("PASSED: %s\n", test_name);
        return 0;
    }
    std::fprintf(stderr, "FAILED: %s (%d expectation(s))\n",
                 test_name, g_failures);
    return 1;
}

}  // namespace panedock::test

#define EXPECT(cond) \
    ::panedock::test::expect((cond), #cond, __FILE__, __LINE__)
