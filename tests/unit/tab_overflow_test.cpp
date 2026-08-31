#include "app_shell/tab_overflow.h"
#include "unit/test_util.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

int main() {
    constexpr auto fits =
        panedock::app_shell::tab_strip_viewport(300, 500, 28);
    static_assert(!fits.overflow);
    static_assert(fits.width == 500);
    static_assert(fits.scroll_button_width == 0);

    constexpr auto overflows =
        panedock::app_shell::tab_strip_viewport(800, 500, 28);
    static_assert(overflows.overflow);
    static_assert(overflows.width == 444);
    static_assert(overflows.scroll_button_width == 28);
    static_assert(panedock::app_shell::clamp_tab_scroll_offset(-1, 800, 444) ==
                  0);
    static_assert(panedock::app_shell::clamp_tab_scroll_offset(123, 800, 444) ==
                  123);
    static_assert(panedock::app_shell::clamp_tab_scroll_offset(999, 800, 444) ==
                  356);

    // PD-085: tab scroll button geometry.
    constexpr auto fwd = panedock::app_shell::tab_scroll_button_visual(
        100, 0, 120, 24, 18, 16, 6, 1, true);
    static_assert(fwd.left == 106);
    static_assert(fwd.top == 5);
    static_assert(fwd.right == 124);
    static_assert(fwd.bottom == 21);
    static_assert(panedock::app_shell::tab_scroll_button_corner_radius(
                      4, fwd.width(), fwd.height()) == 4);
    constexpr int fwd_half = panedock::app_shell::tab_scroll_button_glyph_half(
        5, fwd.width(), fwd.height());
    static_assert(fwd_half == 5);
    constexpr auto fwd_glyph =
        panedock::app_shell::tab_scroll_button_glyph(fwd, fwd_half, true);
    static_assert(fwd_glyph.start_x == 110 && fwd_glyph.start_y == 8);
    static_assert(fwd_glyph.tip_x == 120 && fwd_glyph.tip_y == 13);
    static_assert(fwd_glyph.end_x == 110 && fwd_glyph.end_y == 18);

    constexpr auto back = panedock::app_shell::tab_scroll_button_visual(
        100, 0, 120, 24, 18, 16, 6, 1, false);
    static_assert(back.left == 108);
    static_assert(back.right == 126);
    static_assert(back.top == 5 && back.bottom == 21);
    constexpr auto back_glyph =
        panedock::app_shell::tab_scroll_button_glyph(back, 5, false);
    static_assert(back_glyph.start_x == 122 && back_glyph.start_y == 8);
    static_assert(back_glyph.tip_x == 112 && back_glyph.tip_y == 13);
    static_assert(back_glyph.end_x == 122 && back_glyph.end_y == 18);

    // Visual size larger than the hit-test rect: clamped to the rect.
    constexpr auto clamped = panedock::app_shell::tab_scroll_button_visual(
        0, 0, 10, 6, 18, 16, 0, 0, true);
    static_assert(clamped.left == 0 && clamped.top == 0);
    static_assert(clamped.width() == 10 && clamped.height() == 6);
    static_assert(panedock::app_shell::tab_scroll_button_corner_radius(
                      4, clamped.width(), clamped.height()) == 3);
    static_assert(panedock::app_shell::tab_scroll_button_glyph_half(
                      5, clamped.width(), clamped.height()) == 3);
    static_assert(panedock::app_shell::tab_scroll_button_glyph_half(
                      5, 2, 2) == 2);

    constexpr auto hit_rects =
        panedock::app_shell::tab_scroll_button_hit_rects(
            panedock::app_shell::TabScrollButtonVisual{108, 5, 126, 21},
            panedock::app_shell::TabScrollButtonVisual{126, 5, 144, 21},
            140);
    static_assert(hit_rects[0].left == 104 && hit_rects[0].right == 122);
    static_assert(hit_rects[1].left == 122 && hit_rects[1].right == 140);
    static_assert(hit_rects[0].width() == 18 && hit_rects[1].width() == 18);
    static_assert(hit_rects[0].right <= hit_rects[1].left);

    constexpr auto taller_back =
        panedock::app_shell::tab_scroll_button_visual(
            100, 0, 120, 31, 18, 20, 6, 1, false);
    constexpr auto taller_forward =
        panedock::app_shell::tab_scroll_button_visual(
            120, 0, 140, 31, 18, 20, 6, 1, true);
    constexpr auto taller_hit_rects =
        panedock::app_shell::tab_scroll_button_hit_rects(
            taller_back, taller_forward, 140);
    static_assert(taller_hit_rects[0].height() == 20 &&
                  taller_hit_rects[1].height() == 20);
    static_assert(taller_hit_rects[0].right == taller_hit_rects[1].left &&
                  taller_hit_rects[1].right == 140);

    constexpr auto overlapped =
        panedock::app_shell::tab_scroll_button_hit_rects(
            panedock::app_shell::TabScrollButtonVisual{10, 0, 30, 10},
            panedock::app_shell::TabScrollButtonVisual{25, 0, 45, 10},
            50);
    static_assert(overlapped[0].right == 27 && overlapped[1].left == 27);
    static_assert(overlapped[0].right <= overlapped[1].left);

    const std::vector<int> preferred_widths{100, 120};
    const auto fits_layout = panedock::app_shell::layout_tab_strip(
        {std::span<const int>(preferred_widths), 300, 31, 72, 200, 36, 5, 3,
         20, 18, 20, 6, 1, 0, std::nullopt, std::nullopt});
    EXPECT(fits_layout.tab_rects.size() == preferred_widths.size());
    EXPECT(fits_layout.tab_rects[0].left == 0);
    EXPECT(fits_layout.tab_rects[0].right == 100);
    EXPECT(fits_layout.tab_rects[1].left == 100);
    EXPECT(fits_layout.tab_rects[1].right == 220);
    EXPECT(fits_layout.viewport.width() == 264);
    EXPECT(fits_layout.max_scroll_offset == 0);
    EXPECT(panedock::app_shell::tab_strip_hit_test(
                fits_layout, 110, 10) == std::optional<std::size_t>{1});

    const std::array<int, 5> overflow_widths{100, 100, 100, 100, 100};
    const auto overflow_layout = panedock::app_shell::layout_tab_strip(
        {std::span<const int>(overflow_widths), 300, 31, 72, 200, 36, 5, 3,
         20, 18, 20, 6, 1, 0, std::nullopt, std::nullopt});
    EXPECT(overflow_layout.viewport.width() == 224);
    EXPECT(overflow_layout.max_scroll_offset == 136);
    EXPECT(overflow_layout.scroll_button_rects[0].right >
           overflow_layout.scroll_button_rects[0].left);
    EXPECT(panedock::app_shell::tab_scroll_button_hit_test(
                overflow_layout,
                overflow_layout.scroll_button_rects[1].left + 1, 10) ==
           std::optional<std::size_t>{1});
    EXPECT(panedock::app_shell::tab_scroll_button_hit_test(
                overflow_layout, 1, 10) == std::nullopt);
    EXPECT(panedock::app_shell::tab_scroll_step(overflow_layout, true) == 72);

    const std::array<int, 3> drag_widths{100, 110, 120};
    const auto dragged_layout = panedock::app_shell::layout_tab_strip(
        {std::span<const int>(drag_widths), 300, 31, 72, 200, 36, 5, 3, 20,
         18, 20, 6, 1, 0,
         panedock::app_shell::TabStripDragLayout{
             0, std::optional<std::size_t>{2}, false, 0},
         std::nullopt});
    EXPECT(dragged_layout.placeholder_index ==
           std::optional<std::size_t>{2});
    EXPECT(dragged_layout.tab_rects[0].width() == 0);
    EXPECT(dragged_layout.tab_rects[1].left == 0);
    EXPECT(dragged_layout.tab_rects[2].left ==
           dragged_layout.tab_rects[1].right);
    EXPECT(dragged_layout.placeholder_rect.has_value());
    EXPECT(panedock::app_shell::tab_strip_hit_test(
                dragged_layout, dragged_layout.placeholder_rect->left + 1, 10) ==
           std::optional<std::size_t>{2});

    const auto foreign_layout = panedock::app_shell::layout_tab_strip(
        {std::span<const int>(preferred_widths), 300, 31, 72, 200, 36, 5, 3,
         20, 18, 20, 6, 1, 0,
         panedock::app_shell::TabStripDragLayout{
             0, std::optional<std::size_t>{1}, true, 100},
         std::nullopt});
    EXPECT(foreign_layout.placeholder_index ==
           std::optional<std::size_t>{1});
    EXPECT(foreign_layout.placeholder_rect.has_value());
    EXPECT(panedock::app_shell::tab_strip_hit_test(
                foreign_layout, foreign_layout.placeholder_rect->left + 1, 10) ==
           std::optional<std::size_t>{1});
    return 0;
}
