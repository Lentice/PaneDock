#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <string_view>

namespace panedock::app_shell {

class StartupNotification final {
public:
    StartupNotification() = default;
    StartupNotification(const StartupNotification&) = delete;
    StartupNotification& operator=(const StartupNotification&) = delete;

    static bool register_window_class(HINSTANCE instance) noexcept;

    void append_warning(std::wstring_view warning);
    std::wstring& warning_message() noexcept { return warning_message_; }
    const std::wstring& warning_message() const noexcept {
        return warning_message_;
    }

    void show(HWND owner, HFONT font) noexcept;
    void destroy() noexcept;
    void apply_font(HFONT font) noexcept;

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam);

    void layout(HWND window) noexcept;
    int height(HWND owner, int width) const noexcept;

    HWND window_{nullptr};
    std::wstring warning_message_;
    HFONT font_{nullptr};
};

}  // namespace panedock::app_shell
