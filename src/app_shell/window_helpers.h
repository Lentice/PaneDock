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

bool register_simple_window_class(
    const wchar_t* name, WNDPROC proc, HINSTANCE instance, HBRUSH background,
    UINT style = 0, HICON icon = nullptr, HICON small_icon = nullptr) noexcept;

void center_over_owner(HWND dialog, HWND owner, int width, int height,
                       bool owner_client = false) noexcept;

}  // namespace panedock::app_shell
