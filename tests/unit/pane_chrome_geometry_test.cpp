// The clamps in one pane's chrome geometry.
//
// The interesting cases are the small ones: a pane shorter than its own tab
// strip, shorter than strip plus navigation row, or narrower than seven
// navigation buttons. That arithmetic used to be inline in apply_layout with
// no way to reach it, which is why the same class of defect was reported from
// the desktop three times (PD-107, PD-152, PD-154). None of it needs a window.

#include "app_shell/pane_chrome_geometry.h"
#include "unit/test_util.h"

namespace {

using panedock::app_shell::kNavigationBarHeight;
using panedock::app_shell::kStatusBarHeight;
using panedock::app_shell::kTabStripHeight;
using panedock::app_shell::pane_card_outset;
using panedock::app_shell::pane_card_shadow_offset;
using panedock::app_shell::pane_chrome_rects;
using panedock::app_shell::scale_for_dpi;

constexpr UINT kDpi96 = 96;
constexpr UINT kDpi192 = 192;

bool inside(const RECT& outer, const RECT& inner) noexcept {
    return inner.left >= outer.left && inner.top >= outer.top &&
           inner.right <= outer.right && inner.bottom <= outer.bottom;
}

bool normalized(const RECT& rect) noexcept {
    return rect.right >= rect.left && rect.bottom >= rect.top;
}

// A comfortable pane: every band gets its authored height and the rows stack
// tab strip, navigation, content, status with no gaps and no overlap.
void test_roomy_pane_stacks_every_band() {
    const RECT pane{100, 50, 900, 650};
    const auto rects = pane_chrome_rects(pane, kDpi96);

    EXPECT(rects.tab_strip.top == pane.top);
    EXPECT(rects.tab_strip.bottom == pane.top + kTabStripHeight);
    EXPECT(rects.address_background.top == rects.tab_strip.bottom);
    EXPECT(rects.address_background.bottom ==
           rects.tab_strip.bottom + kNavigationBarHeight);
    EXPECT(rects.explorer_container.top == rects.address_background.bottom);
    EXPECT(rects.explorer_container.bottom == rects.status_bar.top);
    EXPECT(rects.status_bar.bottom == pane.bottom);
    EXPECT(rects.status_bar.bottom - rects.status_bar.top == kStatusBarHeight);

    // Every band spans the pane's full width; only the address row starts
    // after the navigation buttons.
    EXPECT(rects.tab_strip.left == pane.left);
    EXPECT(rects.tab_strip.right == pane.right);
    EXPECT(rects.status_bar.left == pane.left);
    EXPECT(rects.status_bar.right == pane.right);
    EXPECT(rects.explorer_container.left == pane.left);
    EXPECT(rects.explorer_container.right == pane.right);
}

// The pane HWND is the content rect grown by the card outset plus the drop
// shadow, so the shadow has room on the right and bottom only.
void test_pane_window_surrounds_content_and_shadow() {
    const RECT pane{100, 50, 900, 650};
    const auto rects = pane_chrome_rects(pane, kDpi96);
    const int outset = pane_card_outset(kDpi96);
    const int shadow = pane_card_shadow_offset(kDpi96);

    EXPECT(rects.pane_window.left == pane.left - outset);
    EXPECT(rects.pane_window.top == pane.top - outset);
    EXPECT(rects.pane_window.right == pane.right + outset + shadow);
    EXPECT(rects.pane_window.bottom == pane.bottom + outset + shadow);
    EXPECT(inside(rects.pane_window, rects.tab_strip));
    EXPECT(inside(rects.pane_window, rects.status_bar));
    EXPECT(inside(rects.pane_window, rects.explorer_container));
}

// Six buttons laid left to right with no gaps, then the address pill takes
// the rest of the row. The EDIT is inset strictly inside that pill so its
// square corners hide under the rounded ones (PD-031).
void test_navigation_row_packs_buttons_then_address() {
    const RECT pane{0, 0, 800, 600};
    const auto rects = pane_chrome_rects(pane, kDpi96);

    for (std::size_t index = 1; index < rects.navigation_buttons.size();
         ++index) {
        EXPECT(rects.navigation_buttons[index].left ==
               rects.navigation_buttons[index - 1].right);
        EXPECT(rects.navigation_buttons[index].top ==
               rects.navigation_buttons[0].top);
        EXPECT(rects.navigation_buttons[index].bottom ==
               rects.navigation_buttons[0].bottom);
    }
    EXPECT(rects.navigation_buttons.back().right ==
           rects.address_background.left);
    EXPECT(rects.address_background.right == pane.right);

    EXPECT(inside(rects.address_background, rects.address_bar));
    EXPECT(rects.address_bar.left > rects.address_background.left);
    EXPECT(rects.address_bar.top > rects.address_background.top);
    EXPECT(rects.address_bar.right < rects.address_background.right);
    EXPECT(rects.address_bar.bottom < rects.address_background.bottom);
}

// A pane too short for its tab strip: the strip takes what exists, every
// following band collapses to zero height rather than inverting, and nothing
// escapes the pane.
void test_pane_shorter_than_its_tab_strip() {
    const RECT pane{0, 0, 800, 10};
    const auto rects = pane_chrome_rects(pane, kDpi96);

    EXPECT(rects.tab_strip.bottom == pane.bottom);
    EXPECT(rects.address_background.top == pane.bottom);
    EXPECT(rects.address_background.bottom == pane.bottom);
    EXPECT(rects.explorer_container.top == pane.bottom);
    EXPECT(rects.explorer_container.bottom == pane.bottom);
    EXPECT(rects.status_bar.top == pane.bottom);
    EXPECT(normalized(rects.tab_strip));
    EXPECT(normalized(rects.address_background));
    EXPECT(normalized(rects.address_bar));
    EXPECT(normalized(rects.explorer_container));
    EXPECT(normalized(rects.status_bar));
    EXPECT(normalized(rects.folder_context_button));
}

// Room for the strip and part of the navigation row, nothing more: the
// navigation row is truncated and the content area has no height left.
void test_pane_with_a_partial_navigation_row() {
    const RECT pane{0, 0, 800, kTabStripHeight + 10};
    const auto rects = pane_chrome_rects(pane, kDpi96);

    EXPECT(rects.tab_strip.bottom == pane.top + kTabStripHeight);
    EXPECT(rects.address_background.top == pane.top + kTabStripHeight);
    EXPECT(rects.address_background.bottom == pane.bottom);
    EXPECT(rects.explorer_container.top == pane.bottom);
    EXPECT(rects.explorer_container.bottom == pane.bottom);
    EXPECT(normalized(rects.explorer_container));
    EXPECT(normalized(rects.address_bar));
}

// Narrower than seven button widths: the buttons shrink so the address pill
// still has room, instead of spilling past the pane's right edge.
void test_narrow_pane_shrinks_the_navigation_buttons() {
    const RECT pane{0, 0, 70, 600};
    const auto rects = pane_chrome_rects(pane, kDpi96);

    EXPECT(rects.navigation_button_width == 70 / 7);
    EXPECT(rects.navigation_button_width <
           scale_for_dpi(panedock::app_shell::kNavigationButtonWidth, kDpi96));
    EXPECT(rects.navigation_buttons.back().right < pane.right);
    EXPECT(rects.address_background.left < rects.address_background.right);
    for (const RECT& button : rects.navigation_buttons)
        EXPECT(button.right <= pane.right);
}

// A pane of nearly no width at all still yields usable rects: the footer
// action keeps at least one pixel and stays within the pane.
void test_degenerate_pane_keeps_rects_usable() {
    const RECT pane{40, 40, 41, 41};
    const auto rects = pane_chrome_rects(pane, kDpi96);

    EXPECT(normalized(rects.folder_context_button));
    EXPECT(rects.folder_context_button.left >= pane.left);
    EXPECT(rects.folder_context_button.right <= pane.right);
    EXPECT(normalized(rects.explorer_container));
    EXPECT(normalized(rects.address_bar));
    EXPECT(normalized(rects.tab_strip));
}

// The footer action sits inside the status bar it shares a row with, so its
// hover fill cannot paint over the separator or the pane card border.
void test_footer_action_stays_inside_the_status_bar() {
    for (const RECT pane : {RECT{0, 0, 800, 600}, RECT{0, 0, 240, 120},
                            RECT{0, 0, 60, 70}}) {
        const auto rects = pane_chrome_rects(pane, kDpi96);
        EXPECT(inside(rects.status_bar, rects.folder_context_button));
    }
}

// Doubling the DPI doubles the authored bands. The address pill and footer
// action keep their containment, which is the property that actually matters
// on a mixed-DPI move.
void test_high_dpi_scales_the_bands() {
    const RECT pane{0, 0, 1600, 1200};
    const auto rects = pane_chrome_rects(pane, kDpi192);

    EXPECT(rects.tab_strip.bottom - rects.tab_strip.top ==
           kTabStripHeight * 2);
    EXPECT(rects.address_background.bottom - rects.address_background.top ==
           kNavigationBarHeight * 2);
    EXPECT(rects.status_bar.bottom - rects.status_bar.top ==
           kStatusBarHeight * 2);
    EXPECT(inside(rects.address_background, rects.address_bar));
    EXPECT(inside(rects.status_bar, rects.folder_context_button));
}

}  // namespace

int main() {
    test_roomy_pane_stacks_every_band();
    test_pane_window_surrounds_content_and_shadow();
    test_navigation_row_packs_buttons_then_address();
    test_pane_shorter_than_its_tab_strip();
    test_pane_with_a_partial_navigation_row();
    test_narrow_pane_shrinks_the_navigation_buttons();
    test_degenerate_pane_keeps_rects_usable();
    test_footer_action_stays_inside_the_status_bar();
    test_high_dpi_scales_the_bands();
    return panedock::test::summary("pane_chrome_geometry_test");
}
