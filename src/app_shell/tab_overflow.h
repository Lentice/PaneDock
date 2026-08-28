#pragma once

#include <algorithm>
#include <array>

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

// PD-085: pure geometry for the tab scroll buttons, extracted from
// draw_tab_scroll_button so pixel tweaks are testable without a window.
// All inputs are already DPI-scaled by the caller.
struct TabScrollButtonVisual final {
    int left{};
    int top{};
    int right{};
    int bottom{};

    constexpr int width() const noexcept { return right - left; }
    constexpr int height() const noexcept { return bottom - top; }
};

constexpr TabScrollButtonVisual tab_scroll_button_visual(
    int rect_left, int rect_top, int rect_right, int rect_bottom,
    int visual_width, int visual_height, int visual_offset_x,
    int visual_offset_y, bool forward) noexcept {
    const int width = rect_right - rect_left;
    const int height = rect_bottom - rect_top;
    const int clamped_width = std::min(visual_width, width);
    const int clamped_height = std::min(visual_height, height);
    const int top = rect_top + (height - clamped_height) / 2 + visual_offset_y;
    const int left =
        (forward ? rect_left : rect_right - clamped_width) + visual_offset_x;
    return {left, top, left + clamped_width, top + clamped_height};
}

// Keep the painted and hit-tested button rectangles disjoint. There is no
// extra hit-test buffer: the visual rectangle is the hit-test rectangle.
// If the forward visual extends into the add slot, shift both visuals left
// until the complete pair fits. An overlap is split at its midpoint.
constexpr std::array<TabScrollButtonVisual, 2> tab_scroll_button_hit_rects(
    TabScrollButtonVisual back, TabScrollButtonVisual forward,
    int add_left) noexcept {
    const int forward_overflow = std::max(0, forward.right - add_left);
    back.left -= forward_overflow;
    back.right -= forward_overflow;
    forward.left -= forward_overflow;
    forward.right -= forward_overflow;

    const int overlap_left = std::max(back.left, forward.left);
    const int overlap_right = std::min(back.right, forward.right);
    if (overlap_left < overlap_right) {
        const int split = overlap_left + (overlap_right - overlap_left) / 2;
        back.right = split;
        forward.left = split;
    }
    return {back, forward};
}

constexpr int tab_scroll_button_corner_radius(int requested_radius,
                                              int visual_width,
                                              int visual_height) noexcept {
    return std::min(requested_radius,
                    std::min(visual_width, visual_height) / 2);
}

constexpr int tab_scroll_button_glyph_half(int requested_half,
                                           int visual_width,
                                           int visual_height) noexcept {
    return std::max(2, std::min(requested_half,
                                std::min(visual_width, visual_height) / 2));
}

struct TabScrollButtonGlyph final {
    int start_x{};
    int start_y{};
    int tip_x{};
    int tip_y{};
    int end_x{};
    int end_y{};
};

constexpr TabScrollButtonGlyph tab_scroll_button_glyph(
    const TabScrollButtonVisual& visual, int half, bool forward) noexcept {
    const int center_x = (visual.left + visual.right) / 2;
    const int center_y = (visual.top + visual.bottom) / 2;
    const int direction = forward ? 1 : -1;
    return {center_x - direction * half, center_y - half,
            center_x + direction * half, center_y,
            center_x - direction * half, center_y + half};
}

}  // namespace panedock::app_shell
