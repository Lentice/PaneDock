#include "core/layout.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace panedock::core {
namespace {

std::pair<int, int> split(int size, double ratio, int minimum) {
    const int available = std::max(size, kDividerThickness) - kDividerThickness;
    const int first = std::max(minimum, static_cast<int>(std::lround(available * ratio)));
    return {first, std::max(minimum, available - first)};
}

}  // namespace

std::vector<PaneRect> compute_layout_rects(
    int client_width, int client_height, LayoutTemplate layout_template,
    const std::vector<double>& divider_ratios) {
    const int width = std::max(client_width, kMinimumPaneWidth);
    const int height = std::max(client_height, kMinimumPaneHeight);
    const auto ratios = divider_ratios.size() == divider_ratio_count(layout_template)
                            ? divider_ratios
                            : default_divider_ratios(layout_template);

    switch (layout_template) {
        case LayoutTemplate::single:
            return {{0, 0, width, height}};
        case LayoutTemplate::left_right: {
            const auto [left, right] = split(client_width, ratios[0], kMinimumPaneWidth);
            return {{0, 0, left, height},
                    {left + kDividerThickness, 0, right, height}};
        }
        case LayoutTemplate::top_bottom: {
            const auto [top, bottom] = split(client_height, ratios[0], kMinimumPaneHeight);
            return {{0, 0, width, top},
                    {0, top + kDividerThickness, width, bottom}};
        }
        case LayoutTemplate::three_pane: {
            const auto [left, right] = split(client_width, ratios[0], kMinimumPaneWidth);
            const auto [top, bottom] = split(client_height, ratios[1], kMinimumPaneHeight);
            const int right_x = left + kDividerThickness;
            return {{0, 0, left, height},
                    {right_x, 0, right, top},
                    {right_x, top + kDividerThickness, right, bottom}};
        }
        case LayoutTemplate::four_pane_grid: {
            const auto [left, right] = split(client_width, ratios[0], kMinimumPaneWidth);
            const auto [top, bottom] = split(client_height, ratios[1], kMinimumPaneHeight);
            const int right_x = left + kDividerThickness;
            const int bottom_y = top + kDividerThickness;
            return {{0, 0, left, top}, {right_x, 0, right, top},
                    {0, bottom_y, left, bottom},
                    {right_x, bottom_y, right, bottom}};
        }
    }
    return {};
}

}  // namespace panedock::core
