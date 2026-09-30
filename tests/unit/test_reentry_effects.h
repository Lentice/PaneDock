#pragma once

// The test adapter for the ShellReentryEffects seam: records every effect so
// a test can assert what Shell re-entry would have done to real windows.

#include <vector>

#include "app_shell/deferred_messages.h"
#include "app_shell/shell_reentry_guard.h"

namespace panedock::test {

class RecordingReentryEffects final
    : public panedock::app_shell::ShellReentryEffects {
  public:
    HWND main_window{nullptr};
    bool post_succeeds{true};
    // Windows reported as destroyed.
    std::vector<HWND> dead_windows;
    std::vector<panedock::app_shell::DeferredMessage> posted;
    int shutdowns_begun{};

    bool window_alive(HWND window) noexcept override {
        for (HWND dead : dead_windows)
            if (dead == window) return false;
        return true;
    }
    bool post(HWND window, UINT message, WPARAM wparam,
              LPARAM lparam) noexcept override {
        if (!post_succeeds) return false;
        posted.push_back({window, message, wparam, lparam});
        return true;
    }
    HWND shutdown_window() const noexcept override { return main_window; }
    void begin_shutdown_now() noexcept override { ++shutdowns_begun; }
};

}  // namespace panedock::test
