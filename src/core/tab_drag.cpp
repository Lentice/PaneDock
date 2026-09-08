#include "core/tab_drag.h"

namespace panedock::core {

bool begin_tab_drag(TabDragState& state, int x, int y,
                    int threshold) noexcept {
    if (state.dragging) return true;
    const int dx = x - state.start_x;
    const int dy = y - state.start_y;
    if (dx < -threshold || dx > threshold || dy < -threshold ||
        dy > threshold)
        state.dragging = true;
    return state.dragging;
}

bool update_tab_drag(TabDragState& state, const TabStripHit& hit) noexcept {
    if (!state.dragging) return false;

    // A hit on the source pane's own strip is "still home", not a target.
    std::optional<std::size_t> target_pane;
    std::optional<std::size_t> target_index;
    if (hit.pane.has_value()) {
        const bool other_pane = *hit.pane != state.source_pane;
        if (other_pane) target_pane = hit.pane;
        target_index = hit.slot;
        // Past the last tab of another pane means append, so a drop into an
        // empty area still lands rather than being discarded.
        if (!target_index.has_value() && other_pane && hit.in_viewport)
            target_index = hit.tab_count;
    }

    if (target_pane == state.target_pane && target_index == state.target_index)
        return false;
    state.target_pane = target_pane;
    state.target_index = target_index;
    return true;
}

TabDragDrop resolve_tab_drop(const TabDragState& state,
                             std::size_t pane_count) noexcept {
    if (!state.dragging || !state.target_index.has_value()) return {};
    if (state.source_pane >= pane_count) return {};
    const std::size_t target_pane =
        state.target_pane.value_or(state.source_pane);
    if (target_pane >= pane_count) return {};

    if (target_pane == state.source_pane) {
        if (*state.target_index == state.source_index) return {};
        return {TabDragEffect::reorder, state.source_pane, target_pane,
                *state.target_index};
    }
    return {TabDragEffect::move, state.source_pane, target_pane,
            *state.target_index};
}

}  // namespace panedock::core
