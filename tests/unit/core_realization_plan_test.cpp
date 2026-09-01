#include "core/layout.h"
#include "unit/test_util.h"

#include <array>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error "windows.h reached the core test seam"
#endif

namespace {
using namespace panedock::core;

GroupState group_with_panes(LayoutTemplate layout, std::size_t pane_count) {
    GroupState group;
    group.layout_template = layout;
    for (std::size_t index = 0; index < pane_count; ++index) {
        PaneState pane;
        pane.id = "pane-" + std::to_string(index);
        pane.active_tab_id = "tab-" + std::to_string(index);
        TabState tab;
        tab.id = pane.active_tab_id;
        pane.tabs.push_back(std::move(tab));
        group.panes.push_back(std::move(pane));
    }
    group.active_pane_id = group.panes.front().id;
    return group;
}

void expect_indices(const std::vector<std::size_t>& actual,
                    std::initializer_list<std::size_t> expected) {
    EXPECT(actual == std::vector<std::size_t>(expected));
}

void test_all_layouts_respect_visible_pane_bound() {
    const LayoutTemplate layouts[]{
        LayoutTemplate::single,         LayoutTemplate::left_right,
        LayoutTemplate::top_bottom,     LayoutTemplate::three_pane,
        LayoutTemplate::two_over_one,   LayoutTemplate::one_over_two,
        LayoutTemplate::two_beside_one, LayoutTemplate::four_pane_grid};
    for (const LayoutTemplate layout : layouts) {
        const auto group = group_with_panes(layout, pane_count(layout));
        const std::array<bool, kMaxPaneCount> realized{};
        const auto plan = plan_realization(
            group, layout, realized, RealizationMode::normal);
        EXPECT(plan.realize.size() + plan.navigate.size() + plan.keep.size() <=
               pane_count(layout));
        EXPECT(plan.realize.size() == pane_count(layout));
    }
}

void test_shrink_derealizes_hidden_identity() {
    const auto group = group_with_panes(LayoutTemplate::single, kMaxPaneCount);
    const std::array<bool, kMaxPaneCount> realized{true, true, true, true};
    const auto plan = plan_realization(
        group, LayoutTemplate::single, realized, RealizationMode::normal);
    expect_indices(plan.realize, {});
    expect_indices(plan.derealize, {1, 2, 3});
    expect_indices(plan.keep, {0});
}

void test_grow_realizes_existing_hidden_identity() {
    const auto group =
        group_with_panes(LayoutTemplate::four_pane_grid, kMaxPaneCount);
    const std::array<bool, kMaxPaneCount> realized{true, false, false, false};
    const auto plan = plan_realization(
        group, LayoutTemplate::four_pane_grid, realized,
        RealizationMode::normal);
    expect_indices(plan.realize, {1, 2, 3});
    expect_indices(plan.derealize, {});
    expect_indices(plan.keep, {0});
}

void test_group_switch_navigates_live_views() {
    const auto group =
        group_with_panes(LayoutTemplate::four_pane_grid, kMaxPaneCount);
    const std::array<bool, kMaxPaneCount> realized{true, true, true, true};
    const auto plan = plan_realization(
        group, LayoutTemplate::four_pane_grid, realized,
        RealizationMode::group_switch);
    expect_indices(plan.realize, {});
    expect_indices(plan.derealize, {});
    expect_indices(plan.navigate, {0, 1, 2, 3});
    expect_indices(plan.keep, {});
}

void test_startup_deferred_realizes_only_active_pane() {
    auto group = group_with_panes(LayoutTemplate::four_pane_grid, kMaxPaneCount);
    group.active_pane_id = "pane-2";
    const std::array<bool, kMaxPaneCount> realized{};
    const auto plan = plan_realization(
        group, LayoutTemplate::four_pane_grid, realized,
        RealizationMode::startup_deferred);
    expect_indices(plan.realize, {2});
    expect_indices(plan.derealize, {});
    expect_indices(plan.navigate, {});
    expect_indices(plan.keep, {});
}

void test_startup_frame_does_not_realize_views() {
    const auto group =
        group_with_panes(LayoutTemplate::four_pane_grid, kMaxPaneCount);
    const std::array<bool, kMaxPaneCount> realized{};
    const auto plan = plan_realization(
        group, LayoutTemplate::four_pane_grid, realized,
        RealizationMode::startup_frame);
    expect_indices(plan.realize, {});
}

}  // namespace

int main() {
    test_all_layouts_respect_visible_pane_bound();
    test_shrink_derealizes_hidden_identity();
    test_grow_realizes_existing_hidden_identity();
    test_group_switch_navigates_live_views();
    test_startup_deferred_realizes_only_active_pane();
    test_startup_frame_does_not_realize_views();
    return panedock::test::summary("core_realization_plan");
}
