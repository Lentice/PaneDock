#include "core/layout.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace panedock::core {

PaneRect pane_content_rect(const PaneRect& area, int padding,
                           int minimum_pane_width,
                           int minimum_pane_height) noexcept {
    if (area.width < minimum_pane_width + 2 * padding ||
        area.height < minimum_pane_height + 2 * padding)
        return area;
    return {area.x + padding, area.y + padding, area.width - 2 * padding,
            area.height - 2 * padding};
}

bool sidebar_boundary_contains(int x, int boundary, int thickness,
                               int client_left, int client_right) noexcept {
    const int left = (std::max)(client_left, boundary - thickness / 2);
    const int right =
        (std::min)(client_right, boundary + thickness - thickness / 2);
    return right > left && x >= left && x < right;
}

int clamp_sidebar_width(int start_width, int delta) noexcept {
    return std::clamp(start_width + delta, kSidebarMinimumWidth,
                      kSidebarMaximumWidth);
}

LayoutButtonStrip compute_layout_button_strip(
    int client_width, int sidebar_width, int margin, int gap,
    int preferred_button_width, int button_count) noexcept {
    if (button_count <= 0) return {};
    const int gaps = (button_count - 1) * gap;
    const int available =
        (std::max)(0, client_width - sidebar_width - 2 * margin - gaps);
    const int button_width =
        (std::max)(1, (std::min)(preferred_button_width,
                                 available / button_count));
    const int total_width = button_count * button_width + gaps;
    const int x =
        (std::max)(sidebar_width + margin, client_width - margin - total_width);
    return {x, button_width, total_width};
}
namespace {

std::pair<int, int> split(int size, double ratio, int minimum,
                          int divider_thickness) {
    const int available =
        std::max(size, divider_thickness) - divider_thickness;
    const int first = std::max(minimum, static_cast<int>(std::lround(available * ratio)));
    return {first, std::max(minimum, available - first)};
}

}  // namespace

std::vector<PaneRect> compute_layout_rects(
    int client_width, int client_height, LayoutTemplate layout_template,
    const std::vector<double>& divider_ratios, int minimum_pane_width,
    int minimum_pane_height, int divider_thickness) {
    const int width = std::max(client_width, minimum_pane_width);
    const int height = std::max(client_height, minimum_pane_height);
    const auto ratios = divider_ratios.size() == divider_ratio_count(layout_template)
                            ? divider_ratios
                            : default_divider_ratios(layout_template);

    switch (layout_template) {
        case LayoutTemplate::single:
            return {{0, 0, width, height}};
        case LayoutTemplate::left_right: {
            const auto [left, right] = split(
                client_width, ratios[0], minimum_pane_width, divider_thickness);
            return {{0, 0, left, height},
                    {left + divider_thickness, 0, right, height}};
        }
        case LayoutTemplate::top_bottom: {
            const auto [top, bottom] = split(
                client_height, ratios[0], minimum_pane_height,
                divider_thickness);
            return {{0, 0, width, top},
                    {0, top + divider_thickness, width, bottom}};
        }
        case LayoutTemplate::three_pane: {
            const auto [left, right] = split(
                client_width, ratios[0], minimum_pane_width, divider_thickness);
            const auto [top, bottom] = split(
                client_height, ratios[1], minimum_pane_height,
                divider_thickness);
            const int right_x = left + divider_thickness;
            return {{0, 0, left, height},
                    {right_x, 0, right, top},
                    {right_x, top + divider_thickness, right, bottom}};
        }
        case LayoutTemplate::two_over_one: {
            const auto [top, bottom] = split(
                client_height, ratios[0], minimum_pane_height,
                divider_thickness);
            const auto [left, right] = split(
                client_width, ratios[1], minimum_pane_width, divider_thickness);
            const int right_x = left + divider_thickness;
            return {{0, 0, left, top},
                    {right_x, 0, right, top},
                    {0, top + divider_thickness, width, bottom}};
        }
        case LayoutTemplate::one_over_two: {
            const auto [top, bottom] = split(
                client_height, ratios[0], minimum_pane_height,
                divider_thickness);
            const auto [left, right] = split(
                client_width, ratios[1], minimum_pane_width, divider_thickness);
            const int bottom_y = top + divider_thickness;
            return {{0, 0, width, top},
                    {0, bottom_y, left, bottom},
                    {left + divider_thickness, bottom_y, right, bottom}};
        }
        case LayoutTemplate::two_beside_one: {
            const auto [left, right] = split(
                client_width, ratios[0], minimum_pane_width, divider_thickness);
            const auto [top, bottom] = split(
                client_height, ratios[1], minimum_pane_height,
                divider_thickness);
            const int bottom_y = top + divider_thickness;
            return {{0, 0, left, top},
                    {0, bottom_y, left, bottom},
                    {left + divider_thickness, 0, right, height}};
        }
        case LayoutTemplate::four_pane_grid: {
            const auto [left, right] = split(
                client_width, ratios[0], minimum_pane_width, divider_thickness);
            const auto [top, bottom] = split(
                client_height, ratios[1], minimum_pane_height,
                divider_thickness);
            const int right_x = left + divider_thickness;
            const int bottom_y = top + divider_thickness;
            return {{0, 0, left, top}, {right_x, 0, right, top},
                    {0, bottom_y, left, bottom},
                    {right_x, bottom_y, right, bottom}};
        }
    }
    return {};
}

std::vector<SplitterRect> compute_splitter_rects(
    std::span<const PaneRect> pane_rects, LayoutTemplate layout_template,
    int divider_thickness) {
    if (pane_rects.size() < pane_count(layout_template)) return {};
    const int thickness = divider_thickness;
    // Named for readability; every arm below is the same bar-between-panes
    // computation the layout switch above already produced the panes for.
    const auto vertical_bar = [thickness](int x, int y, int height,
                                          std::size_t ratio_index) {
        return SplitterRect{{x, y, thickness, height}, ratio_index, true};
    };
    const auto horizontal_bar = [thickness](int x, int y, int width,
                                            std::size_t ratio_index) {
        return SplitterRect{{x, y, width, thickness}, ratio_index, false};
    };

    switch (layout_template) {
        case LayoutTemplate::single:
            return {};
        case LayoutTemplate::left_right: {
            const PaneRect& a = pane_rects[0];
            return {vertical_bar(a.x + a.width, a.y, a.height, 0)};
        }
        case LayoutTemplate::top_bottom: {
            const PaneRect& a = pane_rects[0];
            return {horizontal_bar(a.x, a.y + a.height, a.width, 0)};
        }
        case LayoutTemplate::three_pane: {
            const PaneRect& a = pane_rects[0];
            const PaneRect& b = pane_rects[1];
            return {vertical_bar(a.x + a.width, a.y, a.height, 0),
                    horizontal_bar(b.x, b.y + b.height, b.width, 1)};
        }
        case LayoutTemplate::four_pane_grid: {
            const PaneRect& a = pane_rects[0];
            const PaneRect& b = pane_rects[1];
            const PaneRect& c = pane_rects[2];
            return {vertical_bar(a.x + a.width, a.y,
                                 c.y + c.height - a.y, 0),
                    horizontal_bar(a.x, a.y + a.height,
                                   b.x + b.width - a.x, 1)};
        }
        case LayoutTemplate::two_over_one: {
            const PaneRect& a = pane_rects[0];
            const PaneRect& c = pane_rects[2];
            return {horizontal_bar(c.x, c.y - thickness, c.width, 0),
                    vertical_bar(a.x + a.width, a.y, a.height, 1)};
        }
        case LayoutTemplate::one_over_two: {
            const PaneRect& b = pane_rects[1];
            const PaneRect& c = pane_rects[2];
            return {horizontal_bar(b.x, b.y - thickness,
                                   c.x + c.width - b.x, 0),
                    vertical_bar(b.x + b.width, b.y, b.height, 1)};
        }
        case LayoutTemplate::two_beside_one: {
            const PaneRect& a = pane_rects[0];
            const PaneRect& c = pane_rects[2];
            return {vertical_bar(c.x - thickness, c.y, c.height, 0),
                    horizontal_bar(a.x, a.y + a.height, a.width, 1)};
        }
    }
    return {};
}

std::optional<double> divider_ratio_at(int position, int size,
                                       int divider_thickness) noexcept {
    const int available = std::max(size, divider_thickness) - divider_thickness;
    if (available <= 0) return std::nullopt;
    return std::clamp(static_cast<double>(position) /
                          static_cast<double>(available),
                      0.0, 1.0);
}

RealizationPlan plan_realization(
    const GroupState& group, LayoutTemplate layout_template,
    std::span<const bool> currently_realized, RealizationMode mode) {
    const std::size_t visible_count = std::min(
        {pane_count(layout_template), group.panes.size(), kMaxPaneCount});
    const std::size_t realized_count =
        std::min(currently_realized.size(), kMaxPaneCount);
    const std::size_t pane_limit = std::max(visible_count, realized_count);

    std::size_t active_pane = 0;
    const auto active = std::find_if(
        group.panes.begin(), group.panes.end(), [&](const PaneState& pane) {
            return pane.id == group.active_pane_id;
        });
    if (active != group.panes.end())
        active_pane = static_cast<std::size_t>(active - group.panes.begin());

    RealizationPlan plan;
    const bool allow_realize = mode != RealizationMode::startup_frame;
    const bool defer_non_active = mode == RealizationMode::startup_deferred;
    const bool navigate_realized = mode == RealizationMode::group_switch;
    for (std::size_t index = 0; index < pane_limit; ++index) {
        const bool realized = index < currently_realized.size() &&
                              currently_realized[index];
        if (index >= visible_count) {
            if (realized) plan.derealize.push_back(index);
            continue;
        }
        if (!realized) {
            if (allow_realize && (!defer_non_active || index == active_pane))
                plan.realize.push_back(index);
            continue;
        }
        (navigate_realized ? plan.navigate : plan.keep).push_back(index);
    }
    return plan;
}

}  // namespace panedock::core
