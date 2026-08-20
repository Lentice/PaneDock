// Placeholder so panedock_core is a valid target before PD-004 lands.
//
// This file exists only to keep `cmake --build` and `ctest` green in a
// greenfield repo, so that PD-001's Agent checks have a working baseline to
// run against. PD-004 replaces it with the real model; delete it then.

namespace panedock::core {

// The one property of this module that is worth asserting today: it compiles
// without windows.h, HWND or any COM type. See AGENTS.md and docs/testing.md.
bool core_is_windows_free() { return true; }

}  // namespace panedock::core
