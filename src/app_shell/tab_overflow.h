#pragma once

#include <algorithm>

namespace panedock::app_shell {

struct TabStripViewport final {
    bool overflow{};
    int width{};
    int scroll_button_width{};
};

constexpr TabStripViewport tab_strip_viewport(int content_width,
                                              int available_width,
                                              int requested_button_width) noexcept {
    const int available = std::max(0, available_width);
    if (content_width <= available)
        return {false, available, 0};

    const int button_width =
        std::min(std::max(0, requested_button_width), available / 2);
    return {true, std::max(0, available - 2 * button_width), button_width};
}

constexpr int clamp_tab_scroll_offset(int requested, int content_width,
                                      int viewport_width) noexcept {
    return std::clamp(requested, 0,
                      std::max(0, content_width - viewport_width));
}

}  // namespace panedock::app_shell
