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

}  // namespace panedock::core
