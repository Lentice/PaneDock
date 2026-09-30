#include "app_shell/window_helpers.h"

#include "app_shell/pane_chrome_geometry.h"

#include <algorithm>

namespace panedock::app_shell {

WindowPositionBatch::WindowPositionBatch() noexcept
    : handle_(BeginDeferWindowPos(static_cast<int>(kCapacity))) {}

WindowPositionBatch::~WindowPositionBatch() {
    if (handle_ != nullptr) (void)EndDeferWindowPos(handle_);
}

void WindowPositionBatch::position(HWND window, HWND insert_after,
                                   const RECT& rect, UINT flags) noexcept {
    if (window == nullptr) return;
    if (entry_count_ == entries_.size()) {
        handle_ = nullptr;
        (void)SetWindowPos(window, insert_after, rect.left, rect.top,
                           rect.right - rect.left, rect.bottom - rect.top,
                           flags);
        return;
    }
    entries_[entry_count_++] = {window, insert_after, rect, flags};
    if (handle_ == nullptr) return;
    const HDWP next = DeferWindowPos(
        handle_, window, insert_after, rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top, flags);
    if (next == nullptr) handle_ = nullptr;
    else handle_ = next;
}

bool WindowPositionBatch::commit() noexcept {
    if (handle_ != nullptr) {
        const HDWP handle = handle_;
        handle_ = nullptr;
        if (EndDeferWindowPos(handle) != FALSE) return true;
    }
    for (std::size_t index = 0; index < entry_count_; ++index) {
        const Entry& entry = entries_[index];
        (void)SetWindowPos(entry.window, entry.insert_after,
                           entry.rect.left, entry.rect.top,
                           entry.rect.right - entry.rect.left,
                           entry.rect.bottom - entry.rect.top, entry.flags);
    }
    return false;
}

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
    return RegisterClassExW(&window_class) != 0 ||
           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

int scaled_value(HWND window, int value) noexcept {
    // One scaling rule, including its floor of 1. This used to be a second
    // copy of pane_chrome_geometry's arithmetic with a comment promising the
    // two matched; nothing enforced that promise.
    return scale_for_dpi(value, GetDpiForWindow(window));
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
