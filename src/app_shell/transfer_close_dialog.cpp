#include "app_shell/transfer_close_dialog.h"

#include <utility>

#include "app_shell/window_helpers.h"

namespace panedock::app_shell {
namespace {

constexpr wchar_t kWindowClassName[] = L"PaneDockTransferCloseDialog";
constexpr int kKeepOpenId = 1;
constexpr int kCloseAfterTransferId = 2;
constexpr int kCancelAndCloseId = 3;
constexpr int kStatusId = 4;

}  // namespace

bool TransferCloseDialog::register_window_class(HINSTANCE instance) noexcept {
    return register_simple_window_class(kWindowClassName, &window_proc,
                                        instance,
                                        reinterpret_cast<HBRUSH>(
                                            COLOR_WINDOW + 1));
}

void TransferCloseDialog::show(HWND owner) noexcept {
    if (window_ != nullptr) {
        SetForegroundWindow(window_);
        return;
    }
    owner_ = owner;
    result_.reset();
    const UINT dpi = GetDpiForWindow(owner);
    const int width = MulDiv(520, static_cast<int>(dpi), 96);
    const int height = MulDiv(170, static_cast<int>(dpi), 96);
    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kWindowClassName,
        L"File transfer in progress", WS_POPUP | WS_CAPTION | WS_SYSMENU, 0,
        0, width, height, owner, nullptr, GetModuleHandleW(nullptr), this);
    if (dialog == nullptr) {
        owner_ = nullptr;
        return;
    }

    center_over_owner(dialog, owner, width, height);
    ShowWindow(dialog, SW_SHOWNORMAL);
    UpdateWindow(dialog);
    SetForegroundWindow(dialog);
}

void TransferCloseDialog::destroy() noexcept {
    if (window_ != nullptr) DestroyWindow(window_);
    owner_ = nullptr;
    result_.reset();
}

std::optional<TransferCloseDialog::Result>
TransferCloseDialog::take_result() noexcept {
    return std::exchange(result_, std::nullopt);
}

void TransferCloseDialog::choose(Result result, bool close) {
    result_ = result;
    const HWND owner = owner_;
    if (close && window_ != nullptr) DestroyWindow(window_);
    notify_owner(owner);
}

void TransferCloseDialog::notify_owner(HWND owner) noexcept {
    if (owner != nullptr && IsWindow(owner))
        SendMessageW(owner, kTransferCloseDialogResultMessage, 0, 0);
}

LRESULT CALLBACK TransferCloseDialog::window_proc(HWND window, UINT message,
                                                   WPARAM wparam,
                                                   LPARAM lparam) {
    auto* self = reinterpret_cast<TransferCloseDialog*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = window_state_from_create<TransferCloseDialog>(window, lparam);
        if (self == nullptr) return FALSE;
        self->window_ = window;
    }

    switch (message) {
        case WM_CREATE: {
            if (self == nullptr) return -1;
            const UINT dpi = GetDpiForWindow(window);
            const auto scaled = [dpi](int value) {
                return MulDiv(value, static_cast<int>(dpi), 96);
            };
            const HINSTANCE instance = GetModuleHandleW(nullptr);
            CreateWindowExW(
                0, L"STATIC", L"A file transfer is still running.",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                scaled(16), scaled(18), scaled(480), scaled(28), window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusId)),
                instance, nullptr);
            CreateWindowExW(
                0, L"BUTTON", L"Keep PaneDock Open",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                scaled(16), scaled(92), scaled(140), scaled(32), window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kKeepOpenId)),
                instance, nullptr);
            CreateWindowExW(
                0, L"BUTTON", L"Close After Transfer",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                scaled(164), scaled(92), scaled(150), scaled(32), window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                    kCloseAfterTransferId)),
                instance, nullptr);
            CreateWindowExW(
                0, L"BUTTON", L"Cancel Transfer and Close",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                scaled(322), scaled(92), scaled(182), scaled(32), window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                    kCancelAndCloseId)),
                instance, nullptr);
            SetFocus(GetDlgItem(window, kCloseAfterTransferId));
            return 0;
        }
        case WM_COMMAND:
            if (self == nullptr) break;
            switch (LOWORD(wparam)) {
                case kKeepOpenId:
                    self->choose(Result::keep_open, true);
                    return 0;
                case kCloseAfterTransferId:
                    self->choose(Result::close_after_transfer, true);
                    return 0;
                case kCancelAndCloseId:
                    self->result_ = Result::cancel_and_close;
                    self->notify_owner(self->owner_);
                    SetWindowTextW(GetDlgItem(window, kStatusId),
                                   L"Cancelling transfer...");
                    EnableWindow(GetDlgItem(window, kKeepOpenId), FALSE);
                    EnableWindow(GetDlgItem(window, kCloseAfterTransferId),
                                 FALSE);
                    EnableWindow(GetDlgItem(window, kCancelAndCloseId), FALSE);
                    return 0;
                default:
                    break;
            }
            break;
        case WM_CLOSE:
            if (self != nullptr) self->choose(Result::keep_open, true);
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
