#pragma once

#include "core/model.h"

#include <optional>
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

// One draggable divider between panes. `ratio_index` indexes
// GroupState::divider_ratios; `vertical` means the divider is a vertical bar
// (dragging it left/right resizes side-by-side panes).
struct SplitterRect final {
    PaneRect rect;
    std::size_t ratio_index{};
    bool vertical{};

    bool operator==(const SplitterRect&) const = default;
};

// The dividers for `layout_template`, derived from the pane rects that
// compute_layout_rects produced for it (in the same coordinate space, so the
// caller may pass already-offset rects and use the result directly).
// Returns empty for `single` or when `pane_rects` is smaller than the
// template needs.
std::vector<SplitterRect> compute_splitter_rects(
    std::span<const PaneRect> pane_rects, LayoutTemplate layout_template,
    int divider_thickness);

// The divider ratio a drag to `position` (relative to the pane content area's
// leading edge, along the divider's axis) means, given that area's `size`.
// Returns nullopt when the area cannot hold a divider, so the caller leaves
// the stored ratio alone rather than persisting a ratio derived from a
// degenerate rect.
std::optional<double> divider_ratio_at(int position, int size,
                                       int divider_thickness) noexcept;

RealizationPlan plan_realization(
    const GroupState& group, LayoutTemplate layout_template,
    std::span<const bool> currently_realized, RealizationMode mode);

}  // namespace panedock::core
