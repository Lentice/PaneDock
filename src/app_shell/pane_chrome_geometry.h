#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>

// Pure geometry of one pane's chrome: given the pane's content rect and the
// window DPI, where every child control goes.
//
// This arithmetic used to live inline in apply_layout, fused with the
// DeferWindowPos batches that consume it. That fusion is why a dozen clamps
// which only bite at small pane sizes had no test at all, while the same class
// of defect was reported three times from the desktop (PD-107, PD-152,
// PD-154). The clamps are the interesting part, so they now live where they
// can be driven without a window: apply_layout still owns placement, batching
// and the parent-scoped DeferWindowPos contract, and only reads these rects.
namespace panedock::app_shell {

constexpr int kPaneCardOutset = 2;
constexpr int kPaneCardShadowOffset = 2;

// Every dimension below is authored at 96 dpi and scaled by scale_for_dpi.
constexpr int kTabStripHeight = 31;
constexpr int kNavigationBarHeight = 28;
constexpr int kNavigationButtonWidth = 32;
constexpr int kNavigationButtonOffsetX = 0;
constexpr int kNavigationButtonOffsetY = 2;
// PD-031: the EDIT is inset well inside the rounded background pill so its
// square corners hide under the pill's rounded corners. The inset exceeds the
// background's radius on purpose; the kNavigationBarHeight budget (28px at 96
// dpi) does not leave room for a taller pill instead.
constexpr int kAddressBarInset = 6;
constexpr int kStatusBarHeight = 24;
constexpr int kPaneFooterVerticalInset = 3;
// Coincides with the sidebar's kSpaceTight today; named for its own role so
// retuning one does not silently move the other.
constexpr int kPaneFooterHorizontalInset = 4;

constexpr int kNavigationButtonCount = 6;

// The one DPI scaling rule in app_shell; app_shell::scaled_value is a thin
// HWND-reading wrapper over it. The floor of 1 is load-bearing: a zero-valued
// constant such as kNavigationButtonOffsetX scales to 1, not 0, and the
// layout has always been authored against that.
inline int scale_for_dpi(int value, UINT dpi) noexcept {
    return (std::max)(1, MulDiv(value, static_cast<int>(dpi), 96));
}

inline int pane_card_outset(UINT dpi) noexcept {
    return scale_for_dpi(kPaneCardOutset, dpi);
}

// Shared by the pane card and the PD-040 explorer-container clip.
inline int pane_card_radius(UINT dpi) noexcept {
    return scale_for_dpi(10, dpi);
}

inline int pane_card_shadow_offset(UINT dpi) noexcept {
    return scale_for_dpi(kPaneCardShadowOffset, dpi);
}

// Insets a rect on all four sides by `inset`, clamping so it never inverts.
inline RECT inset_rect(RECT rect, int inset) noexcept {
    rect.left = (std::min)(rect.right, rect.left + inset);
    rect.top = (std::min)(rect.bottom, rect.top + inset);
    rect.right = (std::max)(rect.left, rect.right - inset);
    rect.bottom = (std::max)(rect.top, rect.bottom - inset);
    return rect;
}

// All rects are in main-window coordinates, the same space as the pane rect
// handed in; apply_layout offsets them into each parent's client space itself.
struct PaneChromeRects final {
    // The pane's own HWND: the content rect grown by the card outset plus the
    // drop shadow, which is the space draw_pane_card paints into.
    RECT pane_window{};
    RECT tab_strip{};
    // Full-width rect the address row occupies, and the rect the rounded
    // background pill is painted into.
    RECT address_background{};
    // The EDIT, inset inside the pill above.
    RECT address_bar{};
    std::array<RECT, kNavigationButtonCount> navigation_buttons{};
    int navigation_button_width{};
    RECT status_bar{};
    RECT folder_context_button{};
    // The PD-040 container HWND that parents the Shell view.
    RECT explorer_container{};
};

inline PaneChromeRects pane_chrome_rects(RECT pane_rect, UINT dpi) noexcept {
    const auto scaled = [dpi](int value) noexcept {
        return scale_for_dpi(value, dpi);
    };
    PaneChromeRects out{};

    const int outset = pane_card_outset(dpi);
    const int shadow = pane_card_shadow_offset(dpi);
    out.pane_window = {pane_rect.left - outset, pane_rect.top - outset,
                       pane_rect.right + outset + shadow,
                       pane_rect.bottom + outset + shadow};

    const int strip_height =
        (std::min)(scaled(kTabStripHeight),
                   static_cast<int>(pane_rect.bottom - pane_rect.top));
    out.tab_strip = {pane_rect.left, pane_rect.top, pane_rect.right,
                     pane_rect.top + strip_height};

    const int navigation_top = pane_rect.top + strip_height;
    const int navigation_height = (std::min)(
        scaled(kNavigationBarHeight),
        (std::max)(0, static_cast<int>(pane_rect.bottom) - navigation_top));
    const int pane_width =
        (std::max)(0, static_cast<int>(pane_rect.right - pane_rect.left));
    // Six buttons plus room left over for the address bar is what a full-width
    // navigation row needs; below that the buttons shrink instead of spilling.
    out.navigation_button_width =
        (std::min)(scaled(kNavigationButtonWidth),
                   static_cast<int>(pane_rect.right - pane_rect.left) / 7);

    const int button_offset_x = scaled(kNavigationButtonOffsetX);
    const int button_offset_y = scaled(kNavigationButtonOffsetY);
    const int button_height = navigation_height - button_offset_y;
    int x = pane_rect.left + button_offset_x;
    for (RECT& button : out.navigation_buttons) {
        button = {x, navigation_top + button_offset_y,
                  x + out.navigation_button_width,
                  navigation_top + button_offset_y + button_height};
        x += out.navigation_button_width;
    }

    out.address_background = {
        pane_rect.left + out.navigation_button_width * 6 + button_offset_x,
        navigation_top, pane_rect.right, navigation_top + navigation_height};
    out.address_bar =
        inset_rect(out.address_background, scaled(kAddressBarInset));

    const int content_top = navigation_top + navigation_height;
    const int status_height = (std::min)(
        scaled(kStatusBarHeight),
        (std::max)(0, static_cast<int>(pane_rect.bottom) - content_top));
    const int footer_top = static_cast<int>(pane_rect.bottom) - status_height;
    out.status_bar = {pane_rect.left, footer_top, pane_rect.right,
                      pane_rect.bottom};

    const int vertical_inset =
        (std::min)(scaled(kPaneFooterVerticalInset),
                   (std::max)(0, (status_height - 1) / 2));
    // Keep the action inside the footer's visual bounds so its hover fill
    // cannot cover the separator or the pane card border.
    const int button_top =
        footer_top + (std::max)(0, vertical_inset - scaled(1));
    const int button_bottom =
        (std::max)(button_top, (std::min)(static_cast<int>(pane_rect.bottom),
                                          static_cast<int>(pane_rect.bottom) -
                                              vertical_inset + scaled(3)));
    const int horizontal_inset =
        (std::min)(scaled(kPaneFooterHorizontalInset),
                   (std::max)(0, (pane_width - 1) / 2));
    // Square by intent: as wide as it is tall, unless the pane is too narrow.
    const int button_width =
        (std::min)((std::max)(1, button_bottom - button_top),
                   (std::max)(1, pane_width - 2 * horizontal_inset));
    const int button_right =
        static_cast<int>(pane_rect.right) - horizontal_inset;
    out.folder_context_button = {
        (std::max)(static_cast<int>(pane_rect.left) + horizontal_inset,
                   button_right - button_width),
        button_top, button_right, button_bottom};

    out.explorer_container = {pane_rect.left, content_top, pane_rect.right,
                              pane_rect.bottom - status_height};
    return out;
}

}  // namespace panedock::app_shell
