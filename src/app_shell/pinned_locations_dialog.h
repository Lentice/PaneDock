#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/model.h"

namespace panedock::app_shell {

class PinnedLocationsDialog final {
public:
    PinnedLocationsDialog() = default;
    PinnedLocationsDialog(const PinnedLocationsDialog&) = delete;
    PinnedLocationsDialog& operator=(const PinnedLocationsDialog&) = delete;

    static bool register_window_class(HINSTANCE instance) noexcept;

    // `display_labels` is parallel to `pinned` only at this call; inside, each
    // row carries its own label.
    void show(HWND owner, const std::vector<core::ShellLocation>& pinned,
              std::vector<std::wstring> display_labels, HFONT font);
    void destroy() noexcept;
    void apply_font(HFONT font) noexcept;
    void add_location(core::ShellLocation location,
                      std::wstring display_label);

    // The pinned list the user applied, once; nullopt when nothing changed.
    std::optional<std::vector<core::ShellLocation>> take_result() noexcept;
    bool is_open() const noexcept { return window_ != nullptr; }
    HWND window() const noexcept { return window_; }

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam);

    void refresh_buttons() noexcept;
    void refresh(std::optional<std::size_t> selected_index = std::nullopt);
    void layout(HWND window) noexcept;
    void apply_draft(bool close);
    std::vector<core::ShellLocation> draft_locations() const;

    HWND window_{nullptr};
    HWND list_{nullptr};
    std::array<HWND, 6> buttons{};
    struct Row final {
        core::ShellLocation location;
        std::wstring label;
    };
    // Engaged while the window exists: the edited rows and the list last
    // applied, which Apply's enabled state compares against.
    std::optional<std::vector<Row>> draft_;
    std::optional<std::vector<core::ShellLocation>> applied_;
    std::optional<std::vector<core::ShellLocation>> result_;
    HFONT font_{nullptr};
};

}  // namespace panedock::app_shell
