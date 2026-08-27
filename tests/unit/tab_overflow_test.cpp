#include "app_shell/tab_overflow.h"

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
    return 0;
}
