#pragma once

#include <cstddef>
#include <optional>

namespace panedock::core {

// A tab drag in progress. The identity of the dragged tab and the window it
// started in belong to the caller; what lives here is the part that decides
// what the gesture means.
struct TabDragState final {
    std::size_t source_pane{};
    std::size_t source_index{};
    int start_x{};
    int start_y{};
    // False until the pointer has moved past the system drag threshold. Until
    // then the gesture is still a click and nothing is drawn.
    bool dragging{};
    // Empty means "still over the source pane"; the source pane is never
    // stored here, so a drag that returns home is indistinguishable from one
    // that never left.
    std::optional<std::size_t> target_pane;
    std::optional<std::size_t> target_index;

    bool operator==(const TabDragState&) const = default;
};

// What the caller measured about the tab strip under the cursor. Producing it
// is the only part of a drag that needs a window: everything after it is
// arithmetic on indices.
struct TabStripHit final {
    // The pane whose tab strip contains the cursor, if any.
    std::optional<std::size_t> pane;
    // The tab slot under the cursor within that strip.
    std::optional<std::size_t> slot;
    // The cursor is inside that strip's tab viewport, which is what lets a
    // drop past the last tab of another pane mean "append".
    bool in_viewport{};
    // That pane's tab count, used for the append slot.
    std::size_t tab_count{};
};

// Promotes a held click to a drag once the pointer has moved past
// `threshold`. Returns whether the gesture is a drag yet: until it is, there
// is nothing to hit-test and nothing to draw, so the caller can stop here.
bool begin_tab_drag(TabDragState& state, int x, int y, int threshold) noexcept;

// Applies one pointer position to a drag that has already begun. Returns true
// when the target moved, so the caller knows which strips to re-lay-out;
// false means nothing moved and no repaint is owed.
bool update_tab_drag(TabDragState& state, const TabStripHit& hit) noexcept;

enum class TabDragEffect {
    none,     // The gesture was a click, was cancelled, or landed where it began.
    reorder,  // Same pane: core::reorder_tab.
    move,     // Across panes: core::move_tab.
};

struct TabDragDrop final {
    TabDragEffect effect{TabDragEffect::none};
    std::size_t source_pane{};
    std::size_t target_pane{};
    std::size_t target_index{};

    bool operator==(const TabDragDrop&) const = default;
};

// What releasing the button means. `pane_count` is the Group's pane count, so
// a drag that outlived a layout switch resolves to `none` rather than
// indexing a pane that is no longer there.
TabDragDrop resolve_tab_drop(const TabDragState& state,
                             std::size_t pane_count) noexcept;

}  // namespace panedock::core
