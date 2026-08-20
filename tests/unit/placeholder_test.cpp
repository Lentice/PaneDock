// Baseline check: panedock_core links and stays free of Windows headers.
// Replaced by the real core tests in PD-004.

#include "unit/test_util.h"

namespace panedock::core {
bool core_is_windows_free();
}

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error "windows.h reached a core test — the core test seam has leaked. See AGENTS.md."
#endif

int main() {
    EXPECT(panedock::core::core_is_windows_free());
    return panedock::test::summary("placeholder");
}
