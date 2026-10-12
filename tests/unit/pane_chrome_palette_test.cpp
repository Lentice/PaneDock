// PD-221: which colors mark the active pane.
//
// The cues only work if they differ where the eye compares them: the active
// pane against the inactive ones, and a tab against the strip behind it.

#include "app_shell/pane_chrome_palette.h"
#include "unit/test_util.h"

#include <initializer_list>

namespace {

using panedock::app_shell::kPaneAccent;
using panedock::app_shell::pane_add_button_hover_colors;
using panedock::app_shell::pane_chrome_colors;
using panedock::app_shell::pane_tab_colors;

void test_only_the_active_pane_has_a_solid_current_tab() {
    const auto active = pane_tab_colors(true, true, false);
    EXPECT(active.fill == kPaneAccent);
    EXPECT(active.text == RGB(255, 255, 255));
    const auto inactive = pane_tab_colors(false, true, false);
    EXPECT(inactive.fill != kPaneAccent);
    EXPECT(inactive.text != RGB(255, 255, 255));
}

void test_active_pane_chrome_is_tinted_and_inactive_is_not() {
    const auto active = pane_chrome_colors(true);
    const auto inactive = pane_chrome_colors(false);
    EXPECT(active.background != inactive.background);
    EXPECT(inactive.background == RGB(255, 255, 255));
    EXPECT(active.text != inactive.text);
    EXPECT(active.glyph != inactive.glyph);
}

void test_tabs_stand_apart_from_their_strip() {
    for (const bool pane_active : {true, false}) {
        const auto strip = pane_chrome_colors(pane_active).background;
        for (const bool tab_active : {true, false}) {
            EXPECT(pane_tab_colors(pane_active, tab_active, false).fill !=
                   strip);
        }
    }
}

void test_hover_changes_an_inactive_tab_in_both_panes() {
    for (const bool pane_active : {true, false}) {
        EXPECT(pane_tab_colors(pane_active, false, true).fill !=
               pane_tab_colors(pane_active, false, false).fill);
    }
}

void test_button_hover_is_visible_on_its_background() {
    for (const bool pane_active : {true, false}) {
        const auto colors = pane_chrome_colors(pane_active);
        EXPECT(colors.button_hover != colors.background);
        EXPECT(colors.button_pressed != colors.button_hover);
    }
}

void test_add_button_hover_is_visible_on_its_strip() {
    for (const bool pane_active : {true, false}) {
        EXPECT(pane_add_button_hover_colors(pane_active).fill !=
               pane_chrome_colors(pane_active).background);
    }
}

}  // namespace

int main() {
    test_only_the_active_pane_has_a_solid_current_tab();
    test_active_pane_chrome_is_tinted_and_inactive_is_not();
    test_tabs_stand_apart_from_their_strip();
    test_hover_changes_an_inactive_tab_in_both_panes();
    test_button_hover_is_visible_on_its_background();
    test_add_button_hover_is_visible_on_its_strip();
    return panedock::test::summary("pane_chrome_palette_test");
}
