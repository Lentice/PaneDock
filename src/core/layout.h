#pragma once

#include "core/model.h"

#include <vector>

namespace panedock::core {

inline constexpr int kMinimumPaneWidth = 120;
inline constexpr int kMinimumPaneHeight = 80;
inline constexpr int kDividerThickness = 4;

struct PaneRect final {
    int x{};
    int y{};
    int width{};
    int height{};

    bool operator==(const PaneRect&) const = default;
};

std::vector<PaneRect> compute_layout_rects(
    int client_width, int client_height, LayoutTemplate layout_template,
    const std::vector<double>& divider_ratios);

std::vector<PaneRect> compute_layout_rects(
    int client_width, int client_height, LayoutTemplate layout_template,
    const std::vector<double>& divider_ratios, int minimum_pane_width,
    int minimum_pane_height, int divider_thickness);

}  // namespace panedock::core
