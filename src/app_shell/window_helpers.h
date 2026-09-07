#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace panedock::app_shell {

template <typename T>
T* window_state_from_create(HWND window, LPARAM lparam) noexcept {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    auto* state =
        create == nullptr ? nullptr : static_cast<T*>(create->lpCreateParams);
    if (state != nullptr)
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(state));
    return state;
}

// A child control's window proc must stop handling messages once teardown has
// been decided, but the frame still has to repaint so "Closing..." is visible
// while the synchronous Shell teardown runs, and WM_NCDESTROY must still be
// delivered so the control can unhook itself.
//
// The three child procs (tab strip, hover tracking, pane controls) shared this
// allowlist by hand. Nothing kept the three copies identical, and a message
// handled during teardown on one path but not another is the shutdown-crash
// class the re-entry gates exist to close. One predicate, one place to change.
//
// Pure: takes the gate's answer rather than reading it, so it is testable
// without a window or an AppState.
inline bool child_message_blocked_while_closing(bool shutting_down,
                                                UINT message) noexcept {
    return shutting_down && message != WM_PAINT &&
           message != WM_ERASEBKGND && message != WM_NCDESTROY;
}

bool register_simple_window_class(
    const wchar_t* name, WNDPROC proc, HINSTANCE instance, HBRUSH background,
    UINT style = 0, HICON icon = nullptr, HICON small_icon = nullptr) noexcept;

int scaled_value(HWND window, int value) noexcept;

void center_over_owner(HWND dialog, HWND owner, int width, int height,
                       bool owner_client = false) noexcept;

}  // namespace panedock::app_shell
