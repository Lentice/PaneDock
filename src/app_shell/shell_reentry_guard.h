#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app_shell/deferred_messages.h"
#include "core/shutdown.h"

namespace panedock::app_shell {

// The only Win32 effects Shell re-entry needs. The app adapter lives in
// main.cpp; tests use a recording fake.
class ShellReentryEffects {
  public:
    virtual ~ShellReentryEffects() = default;
    virtual bool window_alive(HWND window) noexcept = 0;
    virtual bool post(HWND window, UINT message, WPARAM wparam,
                      LPARAM lparam) noexcept = 0;
    // Where the deferred shutdown message goes; nullptr before the main window
    // exists, in which case there is nothing to shut down yet.
    virtual HWND shutdown_window() const noexcept = 0;
    // Posting the deferred shutdown message failed: start shutdown now.
    virtual void begin_shutdown_now() noexcept = 0;
};

// Shell re-entry (CONTEXT.md): interactions that arrive while a Shell call is
// pumping our loop are held and replayed once the outermost call returns, and
// a close the reducer deferred is resumed by a posted message. The call depth
// itself stays in ShutdownSequence, whose resume rules read it (PD-214 D3).
class ShellReentryGuard final {
  public:
    ShellReentryGuard(panedock::core::ShutdownSequence &shutdown,
                      ShellReentryEffects &effects,
                      UINT deferred_shutdown_message) noexcept
        : shutdown_(shutdown), effects_(effects),
          deferred_shutdown_message_(deferred_shutdown_message) {}

    ShellReentryGuard(const ShellReentryGuard &) = delete;
    ShellReentryGuard &operator=(const ShellReentryGuard &) = delete;

    bool in_shell_call() const noexcept {
        return shutdown_.state().shell_call_depth != 0;
    }

    void enter() noexcept {
        shutdown_.step(panedock::core::ShutdownEvent::shell_call_entered);
    }

    void leave() noexcept {
        const auto action =
            shutdown_.step(panedock::core::ShutdownEvent::shell_call_left);
        if (!in_shell_call()) replay_held();
        if (action != panedock::core::ShutdownAction::defer ||
            shutdown_.state().shutdown_message_queued)
            return;
        const HWND window = effects_.shutdown_window();
        if (window == nullptr) return;
        shutdown_.step(panedock::core::ShutdownEvent::deferred_shutdown_queued);
        if (effects_.post(window, deferred_shutdown_message_, 0, 0)) return;
        OutputDebugStringW(L"PaneDock: could not queue deferred shutdown\n");
        shutdown_.step(
            panedock::core::ShutdownEvent::deferred_shutdown_queue_failed);
        effects_.begin_shutdown_now();
    }

    // `replaces` comes from the caller: app-private WM_APP ids cannot be
    // classified by deferred_messages.h (PD-212).
    void hold(HWND target, UINT message, WPARAM wparam, LPARAM lparam,
              bool replaces) noexcept try {
        (void)hold_deferred_message(held_, {target, message, wparam, lparam},
                                    replaces);
    } catch (...) {
        OutputDebugStringW(
            L"PaneDock: could not hold Shell re-entry interaction\n");
    }

  private:
    void replay_held() noexcept {
        if (held_.empty()) return;
        // Take the whole queue first: a replayed message may enter a Shell
        // call again, and what it holds must go to a fresh queue (PD-205).
        const DeferredMessages pending = take_deferred_messages(held_);
        // Derived from the reducer, the same answer AppState gives.
        if (shutdown_.is_shutting_down()) return;
        for (const DeferredMessage &deferred : pending) {
            if (!effects_.window_alive(deferred.target)) continue;
            if (effects_.post(deferred.target, deferred.message,
                              deferred.wparam, deferred.lparam))
                continue;
            OutputDebugStringW(
                L"PaneDock: could not queue Shell re-entry interaction\n");
        }
    }

    panedock::core::ShutdownSequence &shutdown_;
    ShellReentryEffects &effects_;
    UINT deferred_shutdown_message_;
    DeferredMessages held_;
};

// The one scope type for "a Shell call is in progress".
class ShellCallScope final {
  public:
    explicit ShellCallScope(ShellReentryGuard &guard) noexcept
        : guard_(guard) {
        guard_.enter();
    }
    ~ShellCallScope() noexcept { guard_.leave(); }

    ShellCallScope(const ShellCallScope &) = delete;
    ShellCallScope &operator=(const ShellCallScope &) = delete;

  private:
    ShellReentryGuard &guard_;
};

}  // namespace panedock::app_shell
