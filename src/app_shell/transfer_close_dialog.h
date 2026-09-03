#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <optional>

namespace panedock::app_shell {

inline constexpr UINT kTransferCloseDialogResultMessage = WM_APP + 58;

class TransferCloseDialog final {
public:
    enum class Result {
        keep_open,
        close_after_transfer,
        cancel_and_close,
    };

    TransferCloseDialog() = default;
    TransferCloseDialog(const TransferCloseDialog&) = delete;
    TransferCloseDialog& operator=(const TransferCloseDialog&) = delete;

    static bool register_window_class(HINSTANCE instance) noexcept;

    void show(HWND owner) noexcept;
    void destroy() noexcept;
    std::optional<Result> take_result() noexcept;

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam);

    void choose(Result result, bool close);
    void notify_owner(HWND owner) noexcept;

    HWND window_{nullptr};
    HWND owner_{nullptr};
    std::optional<Result> result_;
};

}  // namespace panedock::app_shell
