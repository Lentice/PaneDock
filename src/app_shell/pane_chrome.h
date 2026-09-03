#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <optional>

#include "app_shell/pane_control_id.h"

namespace panedock::app_shell {

class PaneChrome final {
public:
    PaneChrome() noexcept = default;
    ~PaneChrome() { destroy(); }

    PaneChrome(const PaneChrome&) = delete;
    PaneChrome& operator=(const PaneChrome&) = delete;

    bool create(HWND parent, int pane_index) noexcept;
    void destroy() noexcept;
    bool set_rect(const RECT& rect, HDWP* deferred = nullptr) noexcept;
    void set_visible(bool visible) noexcept;
    void apply_font(HFONT font) noexcept;

    HWND explorer_container() const noexcept { return explorer_container_; }
    HWND tab_strip() const noexcept { return tab_strip_; }
    HWND address_bar() const noexcept { return address_bar_; }
    HWND status_bar() const noexcept { return status_bar_; }
    HWND back_button() const noexcept { return back_button_; }
    HWND forward_button() const noexcept { return forward_button_; }
    HWND up_button() const noexcept { return up_button_; }
    HWND refresh_button() const noexcept { return refresh_button_; }
    HWND view_mode_button() const noexcept { return view_mode_button_; }
    HWND pinned_button() const noexcept { return pinned_button_; }

    std::optional<RECT>& laid_out_pane_rect() noexcept {
        return laid_out_pane_rect_;
    }
    const std::optional<RECT>& laid_out_pane_rect() const noexcept {
        return laid_out_pane_rect_;
    }
    bool& tab_tooltips_registered() noexcept {
        return tab_tooltips_registered_;
    }
    bool tab_tooltips_registered() const noexcept {
        return tab_tooltips_registered_;
    }

private:
    HWND explorer_container_{nullptr};
    HWND tab_strip_{nullptr};
    HWND address_bar_{nullptr};
    HWND status_bar_{nullptr};
    HWND back_button_{nullptr};
    HWND forward_button_{nullptr};
    HWND up_button_{nullptr};
    HWND refresh_button_{nullptr};
    HWND view_mode_button_{nullptr};
    HWND pinned_button_{nullptr};
    std::optional<RECT> laid_out_pane_rect_;
    bool tab_tooltips_registered_{};
};

}  // namespace panedock::app_shell
