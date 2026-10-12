#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// PD-221: the colors that mark the active pane. Every cue is a fill or a text
// color on PaneDock's own chrome (tab strip, navigation row, status bar); none
// is a stroke. PD-033/PD-042/PD-045/PD-063 showed that a GDI outline on the
// rounded pane card cannot be drawn with even width or clean corners, so do
// not reintroduce an outline here. The Shell view is never tinted.
namespace panedock::app_shell {

// Same blue as the sidebar's active Group row (PD-076), so the active Group
// and the active pane read as one selection.
constexpr COLORREF kPaneActiveChromeBackground = RGB(234, 241, 255);
constexpr COLORREF kPaneInactiveChromeBackground = RGB(255, 255, 255);
constexpr COLORREF kPaneAccent = RGB(37, 99, 235);

struct PaneChromeColors final {
    COLORREF background;
    COLORREF text;
    COLORREF secondary_text;
    COLORREF glyph;
    COLORREF button_hover;
    COLORREF button_pressed;
};

// The inactive panes recede (gray text and glyphs) so the active one stands
// out by contrast as well as by its tinted chrome.
constexpr PaneChromeColors pane_chrome_colors(bool pane_active) noexcept {
    if (pane_active)
        return {kPaneActiveChromeBackground, RGB(31, 41, 55),
                RGB(100, 116, 139), RGB(90, 102, 122), RGB(219, 230, 250),
                RGB(205, 220, 247)};
    return {kPaneInactiveChromeBackground, RGB(148, 158, 174),
            RGB(160, 170, 185), RGB(160, 170, 185), RGB(242, 245, 248),
            RGB(226, 232, 240)};
}

struct TabColors final {
    COLORREF fill;
    COLORREF border;
    COLORREF text;
};

// Only the active pane's current tab is solid accent. The inactive panes keep
// their current tab distinguishable, but in neutral gray.
constexpr TabColors pane_tab_colors(bool pane_active, bool tab_active,
                                    bool hovered) noexcept {
    const auto chrome = pane_chrome_colors(pane_active);
    if (pane_active) {
        if (tab_active)
            return {kPaneAccent, kPaneAccent, RGB(255, 255, 255)};
        // White chips on the tinted strip: a darker blue fill made the
        // other tabs read muddy next to the solid current tab.
        return {hovered ? RGB(244, 248, 255) : RGB(255, 255, 255),
                hovered ? RGB(176, 198, 238) : RGB(208, 221, 243),
                chrome.text};
    }
    if (tab_active)
        return {RGB(226, 232, 240), RGB(214, 221, 231), RGB(100, 116, 139)};
    return {hovered ? RGB(226, 232, 240) : RGB(244, 246, 248),
            RGB(232, 237, 242), chrome.text};
}

// The tab strip's "+" button only has a surface while hovered. On the active
// pane's tint a neutral gray hover vanished, so it becomes a white chip with
// the hovered-tab border.
constexpr TabColors pane_add_button_hover_colors(bool pane_active) noexcept {
    if (pane_active)
        return {RGB(255, 255, 255), RGB(176, 198, 238),
                pane_chrome_colors(true).text};
    return {RGB(236, 240, 244), RGB(226, 232, 240),
            pane_chrome_colors(false).text};
}

}  // namespace panedock::app_shell
