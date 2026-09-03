#include "app_shell/window_helpers.h"

#include <algorithm>

namespace panedock::app_shell {

bool register_simple_window_class(const wchar_t* name, WNDPROC proc,
                                  HINSTANCE instance, HBRUSH background,
                                  UINT style, HICON icon,
                                  HICON small_icon) noexcept {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = style;
    window_class.hInstance = instance;
    window_class.lpfnWndProc = proc;
    window_class.hIcon = icon;
    window_class.hIconSm = small_icon;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = background;
    window_class.lpszClassName = name;
    return RegisterClassExW(&window_class) != 0;
}

void center_over_owner(HWND dialog, HWND owner, int width, int height,
                       bool owner_client) noexcept {
    if (dialog == nullptr) return;

    RECT owner_rect{};
    int x = 0;
    int y = 0;
    const bool have_owner =
        owner != nullptr &&
        (owner_client ? GetClientRect(owner, &owner_rect)
                      : GetWindowRect(owner, &owner_rect));
    if (have_owner) {
        const int owner_width =
            static_cast<int>(owner_rect.right - owner_rect.left);
        const int owner_height =
            static_cast<int>(owner_rect.bottom - owner_rect.top);
        x = owner_rect.left + std::max(0, (owner_width - width) / 2);
        y = owner_rect.top + std::max(0, (owner_height - height) / 2);
    }
    SetWindowPos(dialog, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
}

}  // namespace panedock::app_shell
