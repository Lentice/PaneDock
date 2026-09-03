#pragma once

#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace panedock::explorer_host {

class PaneErrorOverlay final {
public:
    PaneErrorOverlay() noexcept = default;
    ~PaneErrorOverlay() { destroy(); }

    PaneErrorOverlay(const PaneErrorOverlay&) = delete;
    PaneErrorOverlay& operator=(const PaneErrorOverlay&) = delete;

    bool show(HWND parent, const RECT& rect,
              std::wstring_view location_text) noexcept;
    void hide() noexcept;
    void set_rect(const RECT& rect) noexcept;
    void set_visible(bool visible) noexcept;
    void destroy() noexcept;
    bool visible() const noexcept;
    bool focus() noexcept;
    bool retry_requested() const noexcept;
    void clear_retry_request() noexcept;

private:
    static bool register_window_class() noexcept;
    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam) noexcept;
    void layout_controls() noexcept;

    HWND parent_{nullptr};
    RECT rect_{};
    HWND window_{nullptr};
    HWND message_{nullptr};
    HWND retry_button_{nullptr};
    bool active_{false};
    bool retry_requested_{false};
};

}  // namespace panedock::explorer_host
