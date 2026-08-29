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

}  // namespace

int main() {
    test_all_layouts_have_concrete_coordinates();
    test_dividers_leave_no_gaps_or_overlap();
    test_extreme_ratios_are_clamped();
    test_zero_and_negative_client_area();
    test_client_area_smaller_than_minimums();
    test_mismatched_ratio_count_uses_defaults();
    return panedock::test::summary("core_layout");
}
