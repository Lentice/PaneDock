#include "core/tab_drag.h"
#include "unit/test_util.h"

namespace {
using namespace panedock::core;

// A drag begun on tab 1 of pane 0, at the origin.
TabDragState begun() {
    TabDragState state{};
    state.source_pane = 0;
    state.source_index = 1;
    return state;
}

TabStripHit over(std::size_t pane, std::size_t tab_count) {
    TabStripHit hit{};
    hit.pane = pane;
    hit.tab_count = tab_count;
    return hit;
}

void test_nothing_happens_below_the_drag_threshold() {
    auto state = begun();
    EXPECT(!begin_tab_drag(state, 3, 3, 4));
    EXPECT(!state.dragging);

    // Exactly at the threshold is still a click, in either axis.
    EXPECT(!begin_tab_drag(state, 4, -4, 4));
    EXPECT(!state.dragging);

    // A drag that has not begun ignores the cursor entirely.
    auto hit = over(2, 5);
    hit.slot = 0;
    EXPECT(!update_tab_drag(state, hit));
    EXPECT(!state.target_index.has_value());

    // Crossing the threshold in either axis starts it, and it stays started.
    auto vertical = begun();
    EXPECT(begin_tab_drag(vertical, 0, 5, 4));
    EXPECT(vertical.dragging);
    EXPECT(begin_tab_drag(vertical, 0, 0, 4));
    EXPECT(vertical.dragging);
}

void test_the_source_pane_is_never_a_target_pane() {
    auto state = begun();
    state.dragging = true;
    auto hit = over(0, 3);
    hit.slot = 2;
    EXPECT(update_tab_drag(state, hit));
    // "Still home" is an empty target_pane, not pane 0.
    EXPECT(!state.target_pane.has_value());
    EXPECT(state.target_index == std::optional<std::size_t>{2});
}

void test_a_drop_past_the_last_tab_of_another_pane_appends() {
    auto state = begun();
    state.dragging = true;
    auto hit = over(2, 5);
    hit.in_viewport = true;  // past the last tab, still inside the viewport
    EXPECT(update_tab_drag(state, hit));
    EXPECT(state.target_pane == std::optional<std::size_t>{2});
    EXPECT(state.target_index == std::optional<std::size_t>{5});

    // Outside the viewport there is no slot to append to.
    auto outside = begun();
    outside.dragging = true;
    EXPECT(update_tab_drag(outside, over(2, 5)));
    EXPECT(outside.target_pane == std::optional<std::size_t>{2});
    EXPECT(!outside.target_index.has_value());

    // The same rule does not invent an append slot in the source pane, where
    // empty space means "leave the tab where it is".
    auto home = begun();
    home.dragging = true;
    auto home_hit = over(0, 3);
    home_hit.in_viewport = true;
    EXPECT(!update_tab_drag(home, home_hit));
    EXPECT(!home.target_index.has_value());
}

void test_an_unchanged_target_owes_no_repaint() {
    auto state = begun();
    state.dragging = true;
    auto hit = over(1, 4);
    hit.slot = 0;
    EXPECT(update_tab_drag(state, hit));
    // Same hit again: the state did not move, so nothing needs re-laying out.
    EXPECT(!update_tab_drag(state, hit));
    EXPECT(state.target_pane == std::optional<std::size_t>{1});

    // Leaving every strip clears the target and is a change.
    EXPECT(update_tab_drag(state, TabStripHit{}));
    EXPECT(!state.target_pane.has_value());
    EXPECT(!state.target_index.has_value());
}

void test_a_click_never_drops() {
    auto state = begun();
    state.target_index = 3;  // cannot happen, but must not be trusted either
    EXPECT(resolve_tab_drop(state, 4).effect == TabDragEffect::none);
}

void test_landing_where_it_began_is_not_a_reorder() {
    auto state = begun();
    state.dragging = true;
    state.target_index = 1;  // == source_index
    EXPECT(resolve_tab_drop(state, 4).effect == TabDragEffect::none);

    state.target_index = 2;
    const auto drop = resolve_tab_drop(state, 4);
    EXPECT(drop.effect == TabDragEffect::reorder);
    EXPECT(drop.source_pane == 0);
    EXPECT(drop.target_pane == 0);
    EXPECT(drop.target_index == 2);
}

void test_a_cross_pane_drop_is_a_move() {
    auto state = begun();
    state.dragging = true;
    state.target_pane = 3;
    state.target_index = 0;
    const auto drop = resolve_tab_drop(state, 4);
    EXPECT(drop.effect == TabDragEffect::move);
    EXPECT(drop.source_pane == 0);
    EXPECT(drop.target_pane == 3);
    EXPECT(drop.target_index == 0);

    // Moving onto the same index of another pane is still a move.
    state.target_index = 1;
    EXPECT(resolve_tab_drop(state, 4).effect == TabDragEffect::move);
}

// A drag that outlives a layout switch must not index a pane that is gone.
void test_a_pane_the_group_no_longer_has_drops_nothing() {
    auto state = begun();
    state.dragging = true;
    state.target_pane = 3;
    state.target_index = 0;
    EXPECT(resolve_tab_drop(state, 2).effect == TabDragEffect::none);

    auto from_gone = begun();
    from_gone.source_pane = 3;
    from_gone.dragging = true;
    from_gone.target_index = 0;
    EXPECT(resolve_tab_drop(from_gone, 2).effect == TabDragEffect::none);
}
}  // namespace

int main() {
    test_nothing_happens_below_the_drag_threshold();
    test_the_source_pane_is_never_a_target_pane();
    test_a_drop_past_the_last_tab_of_another_pane_appends();
    test_an_unchanged_target_owes_no_repaint();
    test_a_click_never_drops();
    test_landing_where_it_began_is_not_a_reorder();
    test_a_cross_pane_drop_is_a_move();
    test_a_pane_the_group_no_longer_has_drops_nothing();
    return panedock::test::summary("core_tab_drag");
}
