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

    void show(HWND owner, const core::ApplicationState& application,
              std::vector<std::wstring> display_labels, HFONT font);
    void destroy() noexcept;
    void apply_font(HFONT font) noexcept;
    void add_location(core::ShellLocation location,
                      std::wstring display_label);

    std::optional<core::ApplicationState> take_result() noexcept;
    bool is_open() const noexcept { return window_ != nullptr; }

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam);

    void refresh_buttons() noexcept;
    void refresh(std::optional<std::size_t> selected_index = std::nullopt);
    void layout(HWND window) noexcept;
    void choose_application(bool close);

    HWND window_{nullptr};
    HWND list_{nullptr};
    std::array<HWND, 6> buttons{};
    std::optional<core::ApplicationState> draft_;
    std::optional<core::ApplicationState> applied_;
    std::vector<std::wstring> display_labels_;
    std::optional<core::ApplicationState> result_;
    HFONT font_{nullptr};
};

}  // namespace panedock::app_shell
