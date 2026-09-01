#pragma once

#include "core/model.h"

#include <span>
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

enum class RealizationMode {
    normal,
    startup_frame,
    startup_deferred,
    group_switch,
};

struct RealizationPlan final {
    std::vector<std::size_t> realize;
    std::vector<std::size_t> derealize;
    std::vector<std::size_t> navigate;
    std::vector<std::size_t> keep;
};

std::vector<PaneRect> compute_layout_rects(
    int client_width, int client_height, LayoutTemplate layout_template,
    const std::vector<double>& divider_ratios);

std::vector<PaneRect> compute_layout_rects(
    int client_width, int client_height, LayoutTemplate layout_template,
    const std::vector<double>& divider_ratios, int minimum_pane_width,
    int minimum_pane_height, int divider_thickness);

RealizationPlan plan_realization(
    const GroupState& group, LayoutTemplate layout_template,
    std::span<const bool> currently_realized, RealizationMode mode);

}  // namespace panedock::core
