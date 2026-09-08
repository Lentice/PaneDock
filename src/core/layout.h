#pragma once

#include "core/model.h"

#include <optional>
#include <span>
#include <vector>

namespace panedock::core {

inline constexpr int kMinimumPaneWidth = 120;
inline constexpr int kMinimumPaneHeight = 80;
inline constexpr int kDividerThickness = 4;
inline constexpr int kSidebarMinimumWidth = 160;
inline constexpr int kSidebarMaximumWidth = 420;

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

// The pane area inset by `padding` on every side -- the FR-004a degenerate
// rule. An area that could not hold one minimum-sized pane plus the padding
// keeps its full extent instead: losing the padding is better than losing the
// pane. compute_layout_rects never sees this decision, so it lives here
// beside it rather than in the caller that happens to own an HWND.
PaneRect pane_content_rect(const PaneRect& area, int padding,
                           int minimum_pane_width,
                           int minimum_pane_height) noexcept;

// Whether `x` falls in the draggable strip straddling the sidebar boundary.
// The strip is centred on `boundary` and clipped to the client edges, so a
// boundary sitting on an edge yields a narrower strip rather than one that
// reaches outside the window.
bool sidebar_boundary_contains(int x, int boundary, int thickness,
                               int client_left, int client_right) noexcept;

// The sidebar width a drag means. `delta` is already normalised to 96 DPI,
// so the same physical drag distance produces the same stored width on every
// monitor.
int clamp_sidebar_width(int start_width, int delta) noexcept;

// Where the layout-template button strip goes. The group is right-aligned in
// the header, but never at the cost of overlapping the sidebar: a window too
// narrow to fit it falls back to the left-aligned start, and the buttons
// shrink to whatever room is left.
struct LayoutButtonStrip final {
    int x{};
    int button_width{};
    int total_width{};

    bool operator==(const LayoutButtonStrip&) const = default;
};

LayoutButtonStrip compute_layout_button_strip(
    int client_width, int sidebar_width, int margin, int gap,
    int preferred_button_width, int button_count) noexcept;

}  // namespace panedock::core
