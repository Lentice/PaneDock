#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

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
struct TabStripRect final {
    int left{};
    int top{};
    int right{};
    int bottom{};

    constexpr int width() const noexcept { return right - left; }
    constexpr int height() const noexcept { return bottom - top; }

    constexpr bool contains(int x, int y) const noexcept {
        return left <= x && x < right && top <= y && y < bottom;
    }
};

using TabScrollButtonVisual = TabStripRect;

struct TabStripDragLayout final {
    std::size_t source_index{};
    std::optional<std::size_t> target_index;
    bool foreign_placeholder{};
    int foreign_placeholder_width{};
};

struct TabStripLayoutInput final {
    std::span<const int> preferred_widths;
    int client_width{};
    int client_height{};
    int min_width{};
    int max_width{};
    int add_width{};
    int add_horizontal_inset{};
    int add_vertical_inset{};
    int scroll_button_width{};
    int scroll_visual_width{};
    int scroll_visual_height{};
    int scroll_visual_offset_x{};
    int scroll_visual_offset_y{};
    int requested_scroll_offset{};
    std::optional<TabStripDragLayout> drag;
    std::optional<std::size_t> active_index;
};

struct TabStripGeometry final {
    std::vector<TabStripRect> tab_rects;
    std::optional<TabStripRect> placeholder_rect;
    std::optional<std::size_t> placeholder_index;
    TabStripRect add_rect{};
    TabStripRect viewport{};
    std::array<TabStripRect, 2> scroll_button_rects{};
    int scroll_offset{};
    int max_scroll_offset{};
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

namespace detail {

inline int proportional_width(int width, int available,
                              int total) noexcept {
    const std::int64_t product =
        static_cast<std::int64_t>(width) * available;
    return static_cast<int>((product + total / 2) / total);
}

}  // namespace detail

inline TabStripGeometry layout_tab_strip(
    const TabStripLayoutInput& input) {
    TabStripGeometry geometry;
    const int client_width = std::max(0, input.client_width);
    const int client_height = std::max(0, input.client_height);
    const int min_width = std::max(0, input.min_width);
    const int max_width = std::max(min_width, input.max_width);
    const int available =
        std::max(0, client_width - std::max(0, input.add_width));
    geometry.tab_rects.resize(input.preferred_widths.size());

    std::vector<int> widths;
    widths.reserve(input.preferred_widths.size());
    for (const int preferred : input.preferred_widths) {
        widths.push_back(std::clamp(preferred, min_width, max_width));
    }

    const bool foreign_placeholder =
        input.drag.has_value() &&
        input.drag->foreign_placeholder &&
        input.drag->target_index.has_value();
    int placeholder_width = min_width;
    if (foreign_placeholder) {
        placeholder_width = std::clamp(
            input.drag->foreign_placeholder_width, min_width, max_width);
    }

    const auto sum_widths = [&] {
        return std::accumulate(widths.begin(), widths.end(), 0) +
               (foreign_placeholder ? placeholder_width : 0);
    };
    const int total = sum_widths();
    if (total > available && total > 0) {
        for (int& width : widths) {
            width = std::max(
                min_width,
                detail::proportional_width(width, available, total));
        }
        if (foreign_placeholder) {
            placeholder_width = std::max(
                min_width, detail::proportional_width(
                               placeholder_width, available, total));
        }
    }

    const int content_width = sum_widths();
    const auto viewport = tab_strip_viewport(
        content_width, available, input.scroll_button_width);
    geometry.add_rect = {
        available + input.add_horizontal_inset,
        input.add_vertical_inset,
        client_width - input.add_horizontal_inset,
        client_height - input.add_vertical_inset};
    geometry.viewport = {0, 0, viewport.width, client_height};
    geometry.max_scroll_offset =
        std::max(0, content_width - viewport.width);
    geometry.scroll_offset = clamp_tab_scroll_offset(
        input.requested_scroll_offset, content_width, viewport.width);

    if (viewport.overflow && viewport.scroll_button_width > 0) {
        const int button_left = viewport.width;
        const int button_middle =
            button_left + viewport.scroll_button_width;
        const auto back_visual = tab_scroll_button_visual(
            button_left, 0, button_middle, client_height,
            input.scroll_visual_width, input.scroll_visual_height,
            input.scroll_visual_offset_x, input.scroll_visual_offset_y, false);
        const auto forward_visual = tab_scroll_button_visual(
            button_middle, 0, available, client_height,
            input.scroll_visual_width, input.scroll_visual_height,
            input.scroll_visual_offset_x, input.scroll_visual_offset_y, true);
        geometry.scroll_button_rects = tab_scroll_button_hit_rects(
            back_visual, forward_visual, available);
    }

    std::vector<std::size_t> order(widths.size());
    std::iota(order.begin(), order.end(), 0);
    const bool local_drag =
        input.drag.has_value() &&
        !input.drag->foreign_placeholder &&
        input.drag->target_index.has_value() &&
        input.drag->source_index < order.size() &&
        *input.drag->target_index < order.size();
    if (local_drag) {
        const std::size_t source = input.drag->source_index;
        order.erase(order.begin() + static_cast<std::ptrdiff_t>(source));
        order.insert(order.begin() +
                         static_cast<std::ptrdiff_t>(
                             *input.drag->target_index),
                     source);
    } else if (foreign_placeholder) {
        order.insert(
            order.begin() + static_cast<std::ptrdiff_t>(std::min(
                                *input.drag->target_index, order.size())),
            widths.size());
    }

    int active_left = 0;
    int active_right = 0;
    if (input.active_index.has_value() &&
        *input.active_index < widths.size()) {
        int content_x = 0;
        for (const std::size_t index : order) {
            if (index == widths.size()) {
                content_x += placeholder_width;
            } else if (index == *input.active_index) {
                active_left = content_x;
                active_right = content_x + widths[index];
                break;
            } else {
                content_x += widths[index];
            }
        }
    }

    int requested_offset = input.requested_scroll_offset;
    if (input.active_index.has_value() &&
        *input.active_index < widths.size()) {
        if (active_left < requested_offset) {
            requested_offset = active_left;
        } else if (active_right > requested_offset + viewport.width) {
            requested_offset = active_right - viewport.width;
        }
    }
    geometry.scroll_offset = clamp_tab_scroll_offset(
        requested_offset, content_width, viewport.width);

    int x = 0;
    for (const std::size_t index : order) {
        const int width =
            index == widths.size() ? placeholder_width : widths[index];
        const TabStripRect rect{x - geometry.scroll_offset, 0,
                                x + width - geometry.scroll_offset,
                                client_height};
        if (index == widths.size()) {
            geometry.placeholder_rect = rect;
            geometry.placeholder_index = input.drag->target_index;
        } else if (local_drag && index == input.drag->source_index) {
            geometry.tab_rects[index] = {};
            geometry.placeholder_rect = rect;
            geometry.placeholder_index = input.drag->target_index;
        } else {
            geometry.tab_rects[index] = rect;
        }
        x += width;
    }
    return geometry;
}

inline std::optional<std::size_t> tab_strip_hit_test(
    const TabStripGeometry& geometry, int x, int y) noexcept {
    if (!geometry.viewport.contains(x, y)) return std::nullopt;
    if (geometry.placeholder_rect.has_value() &&
        geometry.placeholder_rect->contains(x, y))
        return geometry.placeholder_index;
    for (std::size_t index = 0; index < geometry.tab_rects.size(); ++index) {
        if (geometry.tab_rects[index].contains(x, y)) return index;
    }
    return std::nullopt;
}

inline std::optional<std::size_t> tab_scroll_button_hit_test(
    const TabStripGeometry& geometry, int x, int y) noexcept {
    if (geometry.scroll_offset > 0 &&
        geometry.scroll_button_rects[0].contains(x, y))
        return 0;
    if (geometry.scroll_offset < geometry.max_scroll_offset &&
        geometry.scroll_button_rects[1].contains(x, y))
        return 1;
    return std::nullopt;
}

inline int tab_scroll_step(const TabStripGeometry& geometry,
                           bool forward) noexcept {
    int edge = forward ? std::numeric_limits<int>::max()
                       : std::numeric_limits<int>::min();
    int step = 0;
    for (const TabStripRect& rect : geometry.tab_rects) {
        if (rect.right <= rect.left ||
            rect.right <= geometry.viewport.left ||
            rect.left >= geometry.viewport.right)
            continue;
        const int candidate = forward ? rect.left : rect.right;
        if ((forward && candidate < edge) ||
            (!forward && candidate > edge)) {
            edge = candidate;
            step = rect.right - rect.left;
        }
    }
    return step;
}

}  // namespace panedock::app_shell
