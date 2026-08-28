#include "core/layout.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace panedock::core {
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
    const std::vector<double>& divider_ratios) {
    return compute_layout_rects(client_width, client_height, layout_template,
                                divider_ratios, kMinimumPaneWidth,
                                kMinimumPaneHeight, kDividerThickness);
}

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

}  // namespace panedock::core
