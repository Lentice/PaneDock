#include "explorer_host/pane_error_overlay.h"

#include <algorithm>
#include <string>

#include "app_shell/window_helpers.h"

namespace panedock::explorer_host {
namespace {

constexpr wchar_t kWindowClassName[] = L"PaneDock.ErrorPanel";
constexpr int kRetryButtonId = 1;

}  // namespace

bool PaneErrorOverlay::register_window_class() noexcept {
    return app_shell::register_simple_window_class(
        kWindowClassName, &window_proc, GetModuleHandleW(nullptr),
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
}

LRESULT CALLBACK PaneErrorOverlay::window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* overlay = reinterpret_cast<PaneErrorOverlay*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        overlay = app_shell::window_state_from_create<PaneErrorOverlay>(
            window, lparam);
    } else if (message == WM_SIZE) {
        if (overlay != nullptr) overlay->layout_controls();
        return 0;
    } else if (message == WM_COMMAND && LOWORD(wparam) == kRetryButtonId &&
               HIWORD(wparam) == BN_CLICKED) {
        if (overlay != nullptr) {
            overlay->retry_requested_ = true;
            PostMessageW(GetAncestor(window, GA_ROOT), WM_NULL, 0, 0);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void PaneErrorOverlay::layout_controls() noexcept {
    if (window_ == nullptr) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int dpi = static_cast<int>(GetDpiForWindow(window_));
    const int padding = MulDiv(16, dpi, 96);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int button_width =
        std::min(MulDiv(80, dpi, 96), std::max(0, width - padding * 2));
    const int button_height =
        std::min(MulDiv(28, dpi, 96), std::max(0, height - padding * 2));
    const int button_x = std::max(0, (width - button_width) / 2);
    const int button_y = std::max(0, height - padding - button_height);
    if (message_ != nullptr) {
        SetWindowPos(message_, nullptr, padding, padding,
                     std::max(0, width - padding * 2),
                     std::max(0, button_y - padding * 2),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (retry_button_ != nullptr) {
        SetWindowPos(retry_button_, nullptr, button_x, button_y,
                     button_width, button_height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

bool PaneErrorOverlay::show(HWND parent, const RECT& rect,
                            std::wstring_view location_text) noexcept {
    if (parent == nullptr) return false;
    parent_ = parent;
    rect_ = rect;
    active_ = true;
    if (window_ == nullptr && register_window_class()) {
        window_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, kWindowClassName, L"",
            WS_CHILD | WS_CLIPCHILDREN, rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top, parent, nullptr,
            GetModuleHandleW(nullptr), this);
    }
    if (window_ == nullptr) return false;
    if (message_ == nullptr) {
        message_ = CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 0, 0, 0, window_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    if (retry_button_ == nullptr) {
        retry_button_ = CreateWindowExW(
            0, L"BUTTON", L"Retry",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRetryButtonId)),
            GetModuleHandleW(nullptr), nullptr);
    }
    try {
        const std::wstring text =
            L"This location is not available:\n" +
            std::wstring(location_text) +
            L"\n\nReconnect the drive or check the path, then retry.";
        SetWindowTextW(message_, text.c_str());
    } catch (...) {
        SetWindowTextW(message_, L"This location is not available.");
    }
    ShowWindow(window_, SW_SHOW);
    set_rect(rect);
    return true;
}

void PaneErrorOverlay::hide() noexcept {
    active_ = false;
    if (window_ != nullptr) ShowWindow(window_, SW_HIDE);
}

void PaneErrorOverlay::set_rect(const RECT& rect) noexcept {
    rect_ = rect;
    if (window_ == nullptr) return;
    SetWindowPos(window_, HWND_TOP, rect.left, rect.top,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOACTIVATE);
    layout_controls();
}

void PaneErrorOverlay::set_visible(bool visible) noexcept {
    if (window_ != nullptr)
        ShowWindow(window_, visible && active_ ? SW_SHOW : SW_HIDE);
}

void PaneErrorOverlay::destroy() noexcept {
    if (window_ != nullptr) DestroyWindow(window_);
    parent_ = nullptr;
    rect_ = {};
    window_ = nullptr;
    message_ = nullptr;
    retry_button_ = nullptr;
    active_ = false;
    retry_requested_ = false;
}

bool PaneErrorOverlay::visible() const noexcept { return active_; }

bool PaneErrorOverlay::focus() noexcept {
    if (!active_ || window_ == nullptr) return false;
    SetFocus(retry_button_ != nullptr ? retry_button_ : window_);
    return true;
}

bool PaneErrorOverlay::retry_requested() const noexcept {
    return retry_requested_;
}

void PaneErrorOverlay::clear_retry_request() noexcept {
    retry_requested_ = false;
}

}  // namespace panedock::explorer_host
