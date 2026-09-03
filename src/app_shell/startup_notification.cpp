#include "app_shell/startup_notification.h"

#include <algorithm>
#include <utility>

#include "app_shell/window_helpers.h"

namespace panedock::app_shell {
namespace {

constexpr wchar_t kWindowClassName[] = L"PaneDockStartupNotification";
constexpr int kTextId = 1;
constexpr int kOkId = 2;
constexpr int kWidth = 560;
constexpr int kMargin = 20;
constexpr int kButtonWidth = 88;
constexpr int kButtonHeight = 28;
constexpr int kButtonGap = 12;

}  // namespace

bool StartupNotification::register_window_class(HINSTANCE instance) noexcept {
    return register_simple_window_class(kWindowClassName, &window_proc,
                                        instance,
                                        reinterpret_cast<HBRUSH>(
                                            COLOR_WINDOW + 1));
}

void StartupNotification::append_warning(std::wstring_view warning) {
    if (warning.empty()) return;
    if (!warning_message_.empty()) warning_message_ += L"\n\n";
    warning_message_.append(warning);
}

void StartupNotification::show(HWND owner, HFONT font) noexcept {
    if (owner == nullptr || warning_message_.empty()) return;
    font_ = font;
    if (window_ != nullptr) {
        const HWND text = GetDlgItem(window_, kTextId);
        if (text != nullptr) {
            SetWindowTextW(text, warning_message_.c_str());
            UpdateWindow(text);
        }
        return;
    }

    const UINT dpi = std::max<UINT>(96, GetDpiForWindow(owner));
    const int width = MulDiv(kWidth, static_cast<int>(dpi), 96);
    const int height_value = height(owner, width);
    // Keep this notification as a child of the root. A visible top-level
    // warning window can be mistaken for Process.MainWindowHandle, causing a
    // close request from the smoke test or a second instance to miss the root.
    HWND dialog = CreateWindowExW(
        WS_EX_CLIENTEDGE | WS_EX_CONTROLPARENT, kWindowClassName, nullptr,
        WS_CHILD | WS_VISIBLE, 0, 0, width, height_value, owner, nullptr,
        GetModuleHandleW(nullptr), this);
    if (dialog == nullptr) {
        OutputDebugStringW(
            L"PaneDock: could not create startup notification\n");
        return;
    }

    center_over_owner(dialog, owner, width, height_value, true);
    UpdateWindow(dialog);
}

void StartupNotification::destroy() noexcept {
    if (window_ != nullptr) DestroyWindow(window_);
}

void StartupNotification::apply_font(HFONT font) noexcept {
    font_ = font;
    if (window_ == nullptr || font == nullptr) return;
    const HWND text = GetDlgItem(window_, kTextId);
    const HWND ok = GetDlgItem(window_, kOkId);
    if (text != nullptr)
        SendMessageW(text, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (ok != nullptr)
        SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void StartupNotification::layout(HWND window) noexcept {
    if (window == nullptr) return;
    const UINT dpi = std::max<UINT>(96, GetDpiForWindow(window));
    const auto scaled = [dpi](int value) {
        return MulDiv(value, static_cast<int>(dpi), 96);
    };
    RECT client{};
    if (!GetClientRect(window, &client)) return;
    const int margin = scaled(kMargin);
    const int gap = scaled(kButtonGap);
    const int button_width = scaled(kButtonWidth);
    const int button_height = scaled(kButtonHeight);
    const int button_x = std::max(
        margin, static_cast<int>(client.right) - margin - button_width);
    const int button_y = std::max(
        margin, static_cast<int>(client.bottom) - margin - button_height);
    const int text_width = std::max(1, static_cast<int>(client.right) -
                                         2 * margin);
    const int text_height = std::max(1, button_y - margin - gap);
    SetWindowPos(GetDlgItem(window, kTextId), nullptr, margin, margin,
                 text_width, text_height, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(window, kOkId), nullptr, button_x, button_y,
                 button_width, button_height, SWP_NOZORDER | SWP_NOACTIVATE);
}

int StartupNotification::height(HWND owner, int width) const noexcept {
    const UINT dpi = std::max<UINT>(96, GetDpiForWindow(owner));
    const auto scaled = [dpi](int value) {
        return MulDiv(value, static_cast<int>(dpi), 96);
    };
    const int margin = scaled(kMargin);
    int text_height = scaled(64);
    HDC dc = GetDC(owner);
    if (dc != nullptr) {
        const HGDIOBJ old_font =
            font_ == nullptr ? nullptr : SelectObject(dc, font_);
        RECT text{0, 0, std::max(1, width - 2 * margin), scaled(1000)};
        if (DrawTextW(dc, warning_message_.c_str(), -1, &text,
                      DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK) != 0)
            text_height = std::max(text_height, static_cast<int>(text.bottom));
        if (old_font != nullptr) SelectObject(dc, old_font);
        ReleaseDC(owner, dc);
    }
    return margin + text_height + scaled(kButtonGap) +
           scaled(kButtonHeight) + margin;
}

LRESULT CALLBACK StartupNotification::window_proc(HWND window, UINT message,
                                                   WPARAM wparam,
                                                   LPARAM lparam) {
    auto* self = reinterpret_cast<StartupNotification*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = window_state_from_create<StartupNotification>(window, lparam);
        if (self == nullptr) return FALSE;
        self->window_ = window;
    }

    switch (message) {
        case WM_CREATE: {
            if (self == nullptr) return -1;
            const HINSTANCE instance = GetModuleHandleW(nullptr);
            const HWND text = CreateWindowExW(
                0, L"STATIC", self->warning_message_.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0,
                window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTextId)),
                instance, nullptr);
            const HWND ok = CreateWindowExW(
                0, L"BUTTON", L"OK",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0,
                0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOkId)), instance,
                nullptr);
            if (text == nullptr || ok == nullptr) return -1;
            self->apply_font(self->font_);
            self->layout(window);
            return 0;
        }
        case WM_SIZE:
            if (self != nullptr) self->layout(window);
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            if (suggested != nullptr)
                SetWindowPos(window, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            if (self != nullptr) self->layout(window);
            return 0;
        }
        case WM_COMMAND:
            if (self != nullptr && LOWORD(wparam) == kOkId &&
                HIWORD(wparam) == BN_CLICKED) {
                self->warning_message_.clear();
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_CLOSE:
            // The recoverable notification has one explicit acknowledgement;
            // the main window's close path destroys it during shutdown.
            return 0;
        case WM_NCDESTROY:
            if (self != nullptr && self->window_ == window) {
                self->window_ = nullptr;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace panedock::app_shell
