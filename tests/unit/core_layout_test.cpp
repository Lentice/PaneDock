#include "core/layout.h"
#include "unit/test_util.h"

#include <limits>
#include <vector>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error "windows.h reached the core test seam"
#endif

namespace {
using namespace panedock::core;

void expect_rects(LayoutTemplate layout, const std::vector<double>& ratios,
                  const std::vector<PaneRect>& expected) {
    EXPECT(compute_layout_rects(1000, 800, layout, ratios) == expected);
}

void test_all_layouts_have_concrete_coordinates() {
    struct Case {
        LayoutTemplate layout;
        std::vector<double> ratios;
        std::vector<PaneRect> expected;
    };
    const Case cases[]{
        {LayoutTemplate::single, {}, {{0, 0, 1000, 800}}},
        {LayoutTemplate::left_right, {0.25},
         {{0, 0, 249, 800}, {253, 0, 747, 800}}},
        {LayoutTemplate::top_bottom, {0.25},
         {{0, 0, 1000, 199}, {0, 203, 1000, 597}}},
        {LayoutTemplate::three_pane, {0.25, 0.75},
         {{0, 0, 249, 800}, {253, 0, 747, 597}, {253, 601, 747, 199}}},
        {LayoutTemplate::two_over_one, {0.25, 0.75},
         {{0, 0, 747, 199}, {751, 0, 249, 199}, {0, 203, 1000, 597}}},
        {LayoutTemplate::one_over_two, {0.25, 0.75},
         {{0, 0, 1000, 199}, {0, 203, 747, 597}, {751, 203, 249, 597}}},
        {LayoutTemplate::two_beside_one, {0.25, 0.75},
         {{0, 0, 249, 597}, {0, 601, 249, 199}, {253, 0, 747, 800}}},
        {LayoutTemplate::four_pane_grid, {0.25, 0.75},
         {{0, 0, 249, 597}, {253, 0, 747, 597},
          {0, 601, 249, 199}, {253, 601, 747, 199}}},
    };
    for (const auto& item : cases) {
        expect_rects(item.layout, item.ratios, item.expected);
    }
}

void test_dividers_leave_no_gaps_or_overlap() {
    const auto rects = compute_layout_rects(
        1001, 801, LayoutTemplate::four_pane_grid, {0.5, 0.5});
    EXPECT(rects == std::vector<PaneRect>({
                        {0, 0, 499, 399}, {503, 0, 498, 399},
                        {0, 403, 499, 398}, {503, 403, 498, 398}}));
    EXPECT(rects[0].width + kDividerThickness + rects[1].width == 1001);
    EXPECT(rects[0].height + kDividerThickness + rects[2].height == 801);
}

void test_extreme_ratios_are_clamped() {
    const auto zero = compute_layout_rects(
        1000, 800, LayoutTemplate::four_pane_grid, {0.0, 0.0});
    const auto one = compute_layout_rects(
        1000, 800, LayoutTemplate::four_pane_grid, {1.0, 1.0});
    EXPECT(zero == std::vector<PaneRect>({
                       {0, 0, 120, 80}, {124, 0, 876, 80},
                       {0, 84, 120, 716}, {124, 84, 876, 716}}));
    EXPECT(one == std::vector<PaneRect>({
                      {0, 0, 996, 796}, {1000, 0, 120, 796},
                      {0, 800, 996, 80}, {1000, 800, 120, 80}}));

    const auto two_over_one = compute_layout_rects(
        1000, 800, LayoutTemplate::two_over_one, {0.0, 0.0});
    EXPECT(two_over_one == std::vector<PaneRect>({
                              {0, 0, 120, 80}, {124, 0, 876, 80},
                              {0, 84, 1000, 716}}));
}

void test_zero_and_negative_client_area() {
    EXPECT(compute_layout_rects(0, std::numeric_limits<int>::min(),
                                LayoutTemplate::left_right, {0.5}) ==
           std::vector<PaneRect>({{0, 0, 120, 80}, {124, 0, 120, 80}}));
}

void test_client_area_smaller_than_minimums() {
    const auto rects = compute_layout_rects(
        100, 100, LayoutTemplate::four_pane_grid, {0.5, 0.5});
    EXPECT(rects == std::vector<PaneRect>({
                        {0, 0, 120, 80}, {124, 0, 120, 80},
                        {0, 84, 120, 80}, {124, 84, 120, 80}}));

    EXPECT(compute_layout_rects(
               100, 100, LayoutTemplate::two_over_one, {0.5, 0.5}) ==
           std::vector<PaneRect>({
               {0, 0, 120, 80}, {124, 0, 120, 80}, {0, 84, 120, 80}}));
    EXPECT(compute_layout_rects(
               100, 100, LayoutTemplate::one_over_two, {0.5, 0.5}) ==
           std::vector<PaneRect>({
               {0, 0, 120, 80}, {0, 84, 120, 80}, {124, 84, 120, 80}}));
    EXPECT(compute_layout_rects(
               100, 100, LayoutTemplate::two_beside_one, {0.5, 0.5}) ==
           std::vector<PaneRect>({
               {0, 0, 120, 80}, {0, 84, 120, 80}, {124, 0, 120, 100}}));
}

void test_mismatched_ratio_count_uses_defaults() {
    EXPECT(compute_layout_rects(1000, 800, LayoutTemplate::three_pane, {0.25}) ==
           compute_layout_rects(1000, 800, LayoutTemplate::three_pane, {0.5, 0.5}));
}

// A splitter must sit exactly in the gap between panes: divider-thick, not
// overlapping any pane, one per divider ratio. This is the property that
// silently breaks when a new LayoutTemplate is added and its arm of
// compute_splitter_rects is copied from the wrong neighbour.
void test_splitters_fill_the_gaps_between_panes() {
    constexpr LayoutTemplate layouts[]{
        LayoutTemplate::single,         LayoutTemplate::left_right,
        LayoutTemplate::top_bottom,     LayoutTemplate::three_pane,
        LayoutTemplate::four_pane_grid, LayoutTemplate::two_over_one,
        LayoutTemplate::one_over_two,   LayoutTemplate::two_beside_one};

    for (const LayoutTemplate layout : layouts) {
        const auto panes = compute_layout_rects(1000, 800, layout, {0.4, 0.6});
        const auto bars = compute_splitter_rects(panes, layout,
                                                 kDividerThickness);
        EXPECT(bars.size() == divider_ratio_count(layout));
        for (const auto& bar : bars) {
            EXPECT(bar.ratio_index < divider_ratio_count(layout));
            EXPECT(bar.vertical ? bar.rect.width == kDividerThickness
                                : bar.rect.height == kDividerThickness);
            // A zero-extent bar is undraggable, and a bar overlapping a pane
            // steals that pane's mouse input.
            EXPECT(bar.rect.width > 0 && bar.rect.height > 0);
            for (const auto& pane : panes) {
                const bool overlaps =
                    bar.rect.x < pane.x + pane.width &&
                    pane.x < bar.rect.x + bar.rect.width &&
                    bar.rect.y < pane.y + pane.height &&
                    pane.y < bar.rect.y + bar.rect.height;
                EXPECT(!overlaps);
            }
        }
    }
}

void test_splitters_reject_too_few_pane_rects() {
    const std::vector<PaneRect> one{{0, 0, 100, 100}};
    EXPECT(compute_splitter_rects(one, LayoutTemplate::four_pane_grid,
                                  kDividerThickness)
               .empty());
}

// A ratio outside [0,1] or NaN is persisted into the session document and
// then feeds compute_layout_rects on every later restore.
void test_divider_ratio_is_clamped_and_guards_degenerate_areas() {
    EXPECT(divider_ratio_at(250, 1004, kDividerThickness) == 0.25);
    // Dragging past either edge pins the divider instead of inverting panes.
    EXPECT(divider_ratio_at(-500, 1004, kDividerThickness) == 0.0);
    EXPECT(divider_ratio_at(5000, 1004, kDividerThickness) == 1.0);
    // An area that cannot hold the divider yields no ratio at all, so the
    // stored one survives a drag during a collapsed layout pass.
    EXPECT(!divider_ratio_at(10, kDividerThickness, kDividerThickness)
                .has_value());
    EXPECT(!divider_ratio_at(10, 0, kDividerThickness).has_value());
    EXPECT(!divider_ratio_at(10, -50, kDividerThickness).has_value());
}

}  // namespace

int main() {
    test_divider_ratio_is_clamped_and_guards_degenerate_areas();
    test_splitters_fill_the_gaps_between_panes();
    test_splitters_reject_too_few_pane_rects();
    test_all_layouts_have_concrete_coordinates();
    test_dividers_leave_no_gaps_or_overlap();
    test_extreme_ratios_are_clamped();
    test_zero_and_negative_client_area();
    test_client_area_smaller_than_minimums();
    test_mismatched_ratio_count_uses_defaults();
    return panedock::test::summary("core_layout");
}
