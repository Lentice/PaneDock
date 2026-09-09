#include "app_shell/pane.h"

#include "app_shell/pane_host.h"
#include "app_shell/window_helpers.h"

#include <commctrl.h>
#include <shlwapi.h>

#include <limits>

#include <array>

namespace panedock::app_shell {
namespace {

constexpr std::array<const wchar_t *, 6> kButtonLabels{
    L"<", L">", L"Up", L"Refresh", L"View", L"Pinned"};
constexpr wchar_t kWindowClassName[] = L"PaneDock.Pane";
// This PC: where a new tab starts and where the last closed tab falls back to.
constexpr wchar_t kDefaultTabParsingName[] =
    L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}";

constexpr std::array<const wchar_t *, 8> kViewModeLabels{
    L"Extra large icons", L"Large icons", L"Medium icons", L"Small icons",
    L"List", L"Details", L"Tiles", L"Content"};

constexpr int kSpaceSnug = 8;
constexpr int kSpaceBase = 12;
constexpr int kTabCornerRadius = 6;
constexpr COLORREF kFooterActionHoverBackground = RGB(236, 240, 244);
constexpr COLORREF kFooterActionBorder = RGB(226, 232, 240);
constexpr COLORREF kFooterActionGlyph = RGB(31, 41, 55);
constexpr int kNavigationGlyphSize = 16;
constexpr COLORREF kStatusBarBackground = RGB(249, 250, 251);
constexpr std::array<wchar_t, 7> kNavigationGlyphs{
    L'\uE72B', L'\uE72A', L'\uE74A', L'\uE72C', L'\uE71D', L'\uE734',
    L'\uE712'};

// PD-052 (refresh) and PD-064 (up) both use the platform icon font: hand-drawn
// GDI geometry could not be centered for even pen widths, while the font glyph
// is always optically centered by DrawText's DT_CENTER | DT_VCENTER.
HFONT& navigation_icon_font(HWND button) noexcept {
    static HFONT font = nullptr;
    if (font == nullptr && button != nullptr) {
        font = CreateFontW(
            -std::max(1, scaled_value(button, kNavigationGlyphSize)), 0, 0, 0,
                           FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                           L"Segoe MDL2 Assets");
    }
    return font;
}

// Draws one Segoe MDL2 Assets glyph centered on the button. Returns false if
// the icon font is unavailable so callers can keep a visible stroke fallback.
bool draw_navigation_font_glyph(const DRAWITEMSTRUCT& item, wchar_t glyph,
                                COLORREF color) noexcept {
    HFONT& icon_font = navigation_icon_font(item.hwndItem);
    if (icon_font == nullptr) return false;
    const HGDIOBJ old_font = SelectObject(item.hDC, icon_font);
    const int old_bk_mode = SetBkMode(item.hDC, TRANSPARENT);
    const COLORREF old_text_color = SetTextColor(item.hDC, color);
    // Center the actual glyph in the final owner-draw button rectangle on
    // both axes, including after footer geometry changes.
    RECT glyph_rect = item.rcItem;
    const int drawn = DrawTextW(item.hDC, &glyph, 1, &glyph_rect,
                                DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                                    DT_NOPREFIX);
    SetTextColor(item.hDC, old_text_color);
    SetBkMode(item.hDC, old_bk_mode);
    SelectObject(item.hDC, old_font);
    return drawn != 0;
}

void draw_navigation_fallback_glyph(const DRAWITEMSTRUCT& item,
                                    std::size_t glyph_kind, COLORREF color,
                                    int size) noexcept {
    const int width = static_cast<int>(item.rcItem.right - item.rcItem.left);
    const int height = static_cast<int>(item.rcItem.bottom - item.rcItem.top);
    const int half = size / 2;
    const int cx = item.rcItem.left + width / 2;
    const int cy = item.rcItem.top + height / 2;

    const int pen_width = std::max(1, size / 8);
    HPEN pen = CreatePen(PS_SOLID, pen_width, color);
    if (pen != nullptr) {
        const HGDIOBJ previous = SelectObject(item.hDC, pen);
        switch (glyph_kind) {
            case 0:  // fallback back: <
                MoveToEx(item.hDC, cx + half / 2, cy - half, nullptr);
                LineTo(item.hDC, cx - half / 2, cy);
                LineTo(item.hDC, cx + half / 2, cy + half);
                break;
            case 1:  // fallback forward: >
                MoveToEx(item.hDC, cx - half / 2, cy - half, nullptr);
                LineTo(item.hDC, cx + half / 2, cy);
                LineTo(item.hDC, cx - half / 2, cy + half);
                break;
            case 2: {  // fallback up
                // Shift the stem right by half the pen width so its center
                // line matches the arrow wings when GDI uses an even width.
                const int stem = cx + pen_width / 2;
                MoveToEx(item.hDC, stem, cy + half, nullptr);
                LineTo(item.hDC, stem, cy - half);
                MoveToEx(item.hDC, stem - half / 2, cy - half / 2, nullptr);
                LineTo(item.hDC, stem, cy - half);
                LineTo(item.hDC, stem + half / 2, cy - half / 2);
                break;
            }
            case 3:  // fallback refresh
                Ellipse(item.hDC, cx - half, cy - half, cx + half, cy + half);
                MoveToEx(item.hDC, cx + half / 2, cy - half, nullptr);
                LineTo(item.hDC, cx, cy - half / 4);
                MoveToEx(item.hDC, cx + half / 2, cy - half, nullptr);
                LineTo(item.hDC, cx + half / 4, cy - half / 2);
                break;
            case 4:  // fallback view: three rows with square bullets
                for (int row = -1; row <= 1; ++row) {
                    const int y = cy + row * half / 2;
                    Rectangle(item.hDC, cx - half, y - 1, cx - half + 3,
                              y + 2);
                    MoveToEx(item.hDC, cx - half / 2, y, nullptr);
                    LineTo(item.hDC, cx + half, y);
                }
                break;
            case 5: {  // fallback pinned location: hollow star
                const std::array<POINT, 10> star{{
                    {cx, cy - half},
                    {cx + half / 3, cy - half / 3},
                    {cx + half, cy - half / 3},
                    {cx + half / 3, cy + half / 8},
                    {cx + half * 3 / 5, cy + half},
                    {cx, cy + half / 2},
                    {cx - half * 3 / 5, cy + half},
                    {cx - half / 3, cy + half / 8},
                    {cx - half, cy - half / 3},
                    {cx - half / 3, cy - half / 3}}};
                MoveToEx(item.hDC, star.front().x, star.front().y, nullptr);
                for (std::size_t point = 1; point < star.size(); ++point)
                    LineTo(item.hDC, star[point].x, star[point].y);
                LineTo(item.hDC, star.front().x, star.front().y);
                break;
            }
            case 6: {  // fallback folder context menu: three dots
                HBRUSH brush = CreateSolidBrush(color);
                if (brush != nullptr) {
                    const int dot = std::max(1, size / 5);
                    const int gap = std::max(1, size / 4);
                    for (int offset = -gap; offset <= gap; offset += gap) {
                        RECT dot_rect{cx + offset - dot / 2,
                                      cy - dot / 2,
                                      cx + offset - dot / 2 + dot,
                                      cy - dot / 2 + dot};
                        FillRect(item.hDC, &dot_rect, brush);
                    }
                    DeleteObject(brush);
                }
                break;
            }
            default:
                break;
        }
        SelectObject(item.hDC, previous);
        DeleteObject(pen);
    }
}

void draw_navigation_icon_button(const DRAWITEMSTRUCT& item,
                                 std::size_t glyph_kind,
                                 bool tracked_hovered,
                                 bool blend_with_footer = false) noexcept {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool hovered = !disabled &&
                         ((item.itemState & ODS_HOTLIGHT) != 0 ||
                          tracked_hovered);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const COLORREF normal_background =
        blend_with_footer ? kStatusBarBackground : RGB(255, 255, 255);
    if (blend_with_footer && hovered) {
        // Erasure is suppressed, so the rounded hover surface must also
        // supply its surrounding footer pixels.
        const COLORREF old_brush_color =
            SetDCBrushColor(item.hDC, normal_background);
        FillRect(item.hDC, &item.rcItem,
                 static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetDCBrushColor(item.hDC, old_brush_color);
        HBRUSH background = CreateSolidBrush(kFooterActionHoverBackground);
        HPEN border = CreatePen(PS_SOLID,
                                scaled_value(item.hwndItem, 1),
                                kFooterActionBorder);
        if (background != nullptr && border != nullptr) {
            const HGDIOBJ old_brush = SelectObject(item.hDC, background);
            const HGDIOBJ old_pen = SelectObject(item.hDC, border);
            const int radius =
                scaled_value(item.hwndItem, kTabCornerRadius);
            RoundRect(item.hDC, item.rcItem.left, item.rcItem.top,
                      item.rcItem.right, item.rcItem.bottom, radius, radius);
            SelectObject(item.hDC, old_pen);
            SelectObject(item.hDC, old_brush);
        }
        if (background != nullptr) DeleteObject(background);
        if (border != nullptr) DeleteObject(border);
    } else {
        const COLORREF background_color =
            disabled ? normal_background
            : !blend_with_footer && pressed ? RGB(226, 232, 240)
            : !blend_with_footer && hovered ? RGB(242, 245, 248)
                                            : normal_background;
        HBRUSH background = CreateSolidBrush(background_color);
        if (background != nullptr) {
            FillRect(item.hDC, &item.rcItem, background);
            DeleteObject(background);
        }
    }

    const COLORREF color = disabled
                               ? RGB(190, 197, 209)
                               : blend_with_footer ? kFooterActionGlyph
                                                   : RGB(90, 102, 122);
    const int size =
        std::max(4, scaled_value(item.hwndItem, kNavigationGlyphSize));
    if (glyph_kind < kNavigationGlyphs.size() &&
        draw_navigation_font_glyph(item, kNavigationGlyphs[glyph_kind],
                                   color)) {
        return;
    }

    // The font-failure path keeps all navigation controls visible without the
    // platform icon font.
    draw_navigation_fallback_glyph(item, glyph_kind, color, size);
}

void draw_status_bar(const DRAWITEMSTRUCT& item, UINT dpi) noexcept {
    const RECT rect = item.rcItem;
    HBRUSH background = CreateSolidBrush(kStatusBarBackground);
    if (background != nullptr) {
        FillRect(item.hDC, &rect, background);
        DeleteObject(background);
    }

    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    const int separator_height = std::min(
        std::max(1, MulDiv(1, static_cast<int>(dpi), 96)), height);
    HBRUSH divider_brush = CreateSolidBrush(RGB(232, 237, 242));
    if (separator_height > 0 && divider_brush != nullptr) {
        RECT separator = rect;
        separator.bottom = separator.top + separator_height;
        FillRect(item.hDC, &separator, divider_brush);
    }

    std::array<wchar_t, 256> text{};
    GetWindowTextW(item.hwndItem, text.data(),
                   static_cast<int>(text.size()));
    const int text_inset = MulDiv(kSpaceBase, static_cast<int>(dpi), 96);
    const int content_top = rect.top + separator_height;
    const int content_bottom = rect.bottom;
    const int text_left = rect.left + text_inset;
    // Leave the larger inset footer action's area available for the
    // full-width separator and keep status text from running underneath it.
    const int footer_action_reserve = scaled_value(
        item.hwndItem,
        kStatusBarHeight - 2 * kPaneFooterVerticalInset +
            2 * kPaneFooterHorizontalInset);
    const int text_right = std::max(
        text_left, static_cast<int>(rect.right) - text_inset -
                       footer_action_reserve);
    const int text_margin = std::max(
        0, MulDiv(kSpaceSnug, static_cast<int>(dpi), 96));
    const int divider_width = std::max(1, MulDiv(1, static_cast<int>(dpi), 96));
    const int divider_height = std::min(
        std::max(1, MulDiv(12, static_cast<int>(dpi), 96)),
        std::max(0, content_bottom - content_top));
    const HFONT font = reinterpret_cast<HFONT>(
        SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font =
        font != nullptr ? SelectObject(item.hDC, font) : nullptr;
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, RGB(100, 116, 139));
    std::array<std::wstring_view, 3> segments{};
    std::size_t segment_count = 0;
    std::wstring_view remaining(text.data());
    while (!remaining.empty() && segment_count < segments.size()) {
        const std::size_t delimiter = remaining.find(L'\t');
        const std::wstring_view segment = remaining.substr(0, delimiter);
        if (!segment.empty()) segments[segment_count++] = segment;
        if (delimiter == std::wstring_view::npos) break;
        remaining.remove_prefix(delimiter + 1);
    }

    int cursor = text_left;
    for (std::size_t index = 0; index < segment_count && cursor < text_right;
         ++index) {
        const auto segment = segments[index];
        SIZE extent{};
        const int length = static_cast<int>(segment.size());
        if (!GetTextExtentPoint32W(item.hDC, segment.data(), length,
                                   &extent)) {
            extent.cx = 0;
        }
        RECT text_rect{cursor, content_top, text_right, content_bottom};
        DrawTextW(item.hDC, segment.data(), length, &text_rect,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        cursor += std::max(0, static_cast<int>(extent.cx));
        if (index + 1 == segment_count) break;

        const int divider_left = cursor + text_margin;
        if (divider_left > text_right - divider_width - text_margin) break;
        if (divider_brush != nullptr && divider_height > 0) {
            RECT divider{divider_left,
                         content_top +
                             std::max(0, (content_bottom - content_top -
                                             divider_height) /
                                            2),
                         divider_left + divider_width,
                         content_top +
                             std::max(0, (content_bottom - content_top -
                                             divider_height) /
                                            2) +
                             divider_height};
            FillRect(item.hDC, &divider, divider_brush);
        }
        cursor = divider_left + divider_width + text_margin;
    }
    if (divider_brush != nullptr) DeleteObject(divider_brush);
    if (old_font != nullptr) SelectObject(item.hDC, old_font);
}

void fill_rounded_rect(HDC dc, const RECT &rect, int radius, COLORREF fill,
                       COLORREF border) noexcept {
    const HGDIOBJ old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    const HGDIOBJ old_pen = SelectObject(
        dc, border == CLR_NONE ? GetStockObject(NULL_PEN)
                               : GetStockObject(DC_PEN));
    SetDCBrushColor(dc, fill);
    if (border != CLR_NONE) SetDCPenColor(dc, border);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
}

// Same fill color as draw_navigation_bar_background's RoundRect, returned as
// a cached HBRUSH for WM_CTLCOLOREDIT so the address bar's native background
// matches the rounded pill painted underneath it (PD-031 decision 2). Kept
// as a single process-lifetime brush per the ticket's suggested "static
// brush freed at process lifetime" pattern; released in WM_DESTROY.
HBRUSH address_bar_background_brush() noexcept {
    static HBRUSH brush = CreateSolidBrush(RGB(251, 252, 253));
    return brush;
}

LRESULT CALLBACK address_edit_proc(HWND window, UINT message, WPARAM wparam,
                                   LPARAM lparam, UINT_PTR subclass_id,
                                   DWORD_PTR reference_data) {
    auto* pane = reinterpret_cast<Pane*>(reference_data);
    // First click into an unfocused address bar selects everything so the user
    // can paste over the path. The EDIT places its caret in WM_LBUTTONDOWN,
    // after WM_SETFOCUS, so selecting there would be cleared immediately.
    if (message == WM_LBUTTONDOWN && GetFocus() != window) {
        SetFocus(window);
        SendMessageW(window, EM_SETSEL, 0, -1);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_RETURN && pane != nullptr) {
        pane->submit_address();
        return 0;
    }
    if (message == WM_CHAR && wparam == VK_RETURN) return 0;
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, address_edit_proc, subclass_id);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

void set_font(HWND window, HFONT font) noexcept {
    if (window != nullptr && font != nullptr)
        SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

LRESULT CALLBACK navigation_button_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam,
                                        UINT_PTR subclass_id, DWORD_PTR) {
    // Owner draw supplies the background. Native BUTTON erasure otherwise
    // exposes a blank frame during resize, before WM_DRAWITEM arrives.
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, navigation_button_proc, subclass_id);
    return DefSubclassProc(window, message, wparam, lparam);
}

void destroy_window(HWND &window) noexcept {
    if (window != nullptr) {
        DestroyWindow(window);
        window = nullptr;
    }
}

LRESULT CALLBACK pane_window_proc(HWND window, UINT message, WPARAM wparam,
                                  LPARAM lparam) {
    auto *pane =
        reinterpret_cast<Pane *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        pane = window_state_from_create<Pane>(window, lparam);
        if (pane == nullptr)
            return FALSE;
    }

    switch (message) {
    case WM_ERASEBKGND: {
        if (pane != nullptr)
            pane->paint_background(reinterpret_cast<HDC>(wparam));
        return 1;
    }
    case WM_COMMAND:
    case WM_DRAWITEM:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
    case WM_MEASUREITEM: {
        // PD-189: this pane's controls are handled here, not forwarded.
        if (pane == nullptr) break;
        if (pane->pane_host() == nullptr) break;
        const auto handled = pane->pane_host()->handle_pane_control_message(
            *pane, message, wparam, lparam);
        if (handled.has_value()) return *handled;
        break;
    }
    case WM_NCHITTEST: {
        // PD-187 grows this HWND past pane_rect by the card outset (and the
        // shadow on the trailing edges) so the card border paints into the gap
        // between panes. That growth otherwise eats most of the splitter's
        // kDividerThickness-wide hit strip on the main window, which is why
        // splitter drags only caught every few attempts. The margin holds
        // decoration only, so let the main window have those pixels back.
        POINT point{static_cast<short>(LOWORD(lparam)),
                    static_cast<short>(HIWORD(lparam))};
        RECT client{};
        if (!ScreenToClient(window, &point) || !GetClientRect(window, &client))
            break;
        const UINT dpi = GetDpiForWindow(window);
        const int outset = pane_card_outset(dpi);
        const int trailing = outset + pane_card_shadow_offset(dpi);
        if (point.x < outset || point.y < outset ||
            point.x >= client.right - trailing ||
            point.y >= client.bottom - trailing)
            return HTTRANSPARENT;
        break;
    }
    // Handle the tab strip menu here because it belongs to this pane.
    // The host supplies the shutdown and Shell re-entry gates.
    case WM_CONTEXTMENU: {
        if (pane == nullptr || pane->pane_host() == nullptr) break;
        if (reinterpret_cast<HWND>(wparam) != pane->tab_strip()) break;
        const POINT screen{static_cast<short>(LOWORD(lparam)),
                           static_cast<short>(HIWORD(lparam))};
        // Keyboard-invoked (Shift+F10): no anchor to hit-test against.
        if (screen.x == -1 && screen.y == -1) break;
        const auto gated = pane->pane_host()->handle_pane_control_message(
            *pane, message, wparam, lparam);
        if (gated.has_value()) return *gated;
        pane->handle_tab_context_menu(screen);
        return 0;
    }
    case WM_NOTIFY:
        return SendMessageW(GetParent(window), message, wparam, lparam);
    case WM_DESTROY:
        if (pane != nullptr) {
            // Fail-safe for parent-chain destruction. The normal §9.4
            // path is still destroy_panes() before the main HWND dies.
            pane->derealize();
        }
        break;
    case WM_NCDESTROY:
        if (pane != nullptr)
            pane->window_destroyed(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

void Pane::release_address_bar_background_brush() noexcept {
    // The static above is a function-local singleton; DeleteObject is safe
    // to call on it more than once only if we null it out, but WM_DESTROY
    // fires exactly once per window, so a single delete here is sufficient.
    HBRUSH brush = address_bar_background_brush();
    if (brush != nullptr) DeleteObject(brush);
}

void Pane::release_navigation_icon_font() noexcept {
    HFONT& font = navigation_icon_font(nullptr);
    if (font != nullptr) {
        DeleteObject(font);
        font = nullptr;
    }
}

bool Pane::handle_command(int id) {
    const int view_mode_base = kViewModeMenuIdBase + static_cast<int>(
        index_ * panedock::shell_core::kViewModeOptions.size());
    if (id >= view_mode_base && id < view_mode_base +
            static_cast<int>(panedock::shell_core::kViewModeOptions.size())) {
        set_view_mode(panedock::shell_core::kViewModeOptions[
            static_cast<std::size_t>(id - view_mode_base)]);
        return true;
    }
    const int pinned_base =
        kPinnedMenuIdBase + static_cast<int>(index_ * kPinnedMenuSlotsPerPane);
    if (id >= pinned_base && id < pinned_base + kPinnedMenuManageOffset) {
        if (!active() ||
            pane_state() == nullptr) return true;
        const int item = id - pinned_base;
        const auto locations = pane_host()->pinned_locations();
        if (locations.size() < kPinnedMenuFixedLocationCount) return true;
        if (item == kPinnedMenuDesktopOffset ||
            item == kPinnedMenuThisPcOffset) {
            ShellCall shell_call(pane_host());
            (void)navigate_to(
                locations[static_cast<std::size_t>(item)].location);
            return true;
        }
        if (item >= kPinnedMenuLocationOffset && item < kPinnedMenuAddOffset) {
            const std::size_t location_index =
                static_cast<std::size_t>(item - kPinnedMenuLocationOffset);
            const std::size_t record_index =
                kPinnedMenuFixedLocationCount + location_index;
            if (record_index < locations.size()) {
                ShellCall shell_call(pane_host());
                (void)navigate_to(locations[record_index].location);
            }
            return true;
        }
        if (item == kPinnedMenuAddOffset) {
            pin_current_folder();
            return true;
        }
    }

    switch (decode_pane_control(id).value_or(PaneControl::tab_strip)) {
        case PaneControl::back:
            navigate_history(true);
            return true;
        case PaneControl::forward:
            navigate_history(false);
            return true;
        case PaneControl::up:
            navigate_up();
            return true;
        case PaneControl::refresh:
            refresh_view();
            return true;
        case PaneControl::view_mode:
            {
                RECT rect{};
                if (GetWindowRect(view_mode_button(), &rect))
                    show_view_mode_menu({rect.left, rect.bottom});
            }
            return true;
        case PaneControl::pinned:
            {
                RECT rect{};
                if (GetWindowRect(pinned_button(), &rect))
                    show_pinned_locations_menu({rect.left, rect.bottom});
            }
            return true;
        default: return false;
    }
}

bool Pane::draw_control(const DRAWITEMSTRUCT& item) {
    // The status bar is an SS_OWNERDRAW STATIC with no control id.
    if (item.hwndItem == status_bar()) {
        draw_status_bar(item, GetDpiForWindow(item.hwndItem));
        return true;
    }
    if (item.CtlType != ODT_BUTTON) return false;
    const auto control = decode_pane_control(item.CtlID);
    if (!control.has_value()) return false;
    struct PaneButtonDrawing final {
        PaneControl control;
        std::size_t glyph;
        bool blend;
    };
    constexpr std::array pane_button_drawings{
        PaneButtonDrawing{PaneControl::back, 0, false},
        PaneButtonDrawing{PaneControl::forward, 1, false},
        PaneButtonDrawing{PaneControl::up, 2, false},
        PaneButtonDrawing{PaneControl::refresh, 3, false},
        PaneButtonDrawing{PaneControl::view_mode, 4, false},
        PaneButtonDrawing{PaneControl::pinned, 5, false},
        PaneButtonDrawing{PaneControl::folder_context, 6, true}};
    const auto drawing = std::find_if(
        pane_button_drawings.begin(), pane_button_drawings.end(),
        [&](const auto& candidate) {
            return candidate.control == *control;
        });
    if (drawing == pane_button_drawings.end()) return false;
    draw_navigation_icon_button(item, drawing->glyph, false, drawing->blend);
    return true;
}

std::optional<LRESULT> Pane::color_address_bar(HWND control, HDC dc) noexcept {
    if (control != address_bar()) return std::nullopt;
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, RGB(251, 252, 253));
    SetTextColor(dc, RGB(76, 89, 107));
    return reinterpret_cast<LRESULT>(address_bar_background_brush());
}

void Pane::refresh_navigation_chrome() {
    if (pane_host() == nullptr) return;
    refresh_navigation_buttons();
    std::wstring text;
    if (auto* bound = pane_state()) {
        const std::wstring parsing_name =
            active_tab()->location.parsing_name;
        if (parsing_name.starts_with(L"::")) {
            ShellCall shell_call(pane_host());
            text = panedock::shell_core::display_text_for_parsing_name(
                parsing_name);
        } else {
            text = parsing_name;
        }
        auto* current = pane_state();
        if (current != bound ||
            active_tab()->location.parsing_name != parsing_name)
            return;
    }
    SetWindowTextW(address_bar(), text.c_str());
}

void Pane::refresh_status_bar() noexcept {
    if (pane_host() == nullptr) return;
    if (status_bar() == nullptr) return;
    panedock::explorer_host::ExplorerHost::ItemCounts counts;
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCall shell_call(pane_host());
        hr = host().item_counts(counts);
    }
    if (!active()) return;
    if (FAILED(hr)) {
        SetWindowTextW(status_bar(), L"");
        return;
    }
    std::wstring text = std::to_wstring(counts.total) + L" items";
    if (counts.selected != 0) {
        text += L'\t';
        text += std::to_wstring(counts.selected);
        text += L" selected";
        if (counts.selected_bytes_valid && counts.selected_bytes != 0) {
            std::array<wchar_t, 64> size_text{};
            const auto bytes = std::min(
                counts.selected_bytes,
                static_cast<unsigned long long>(
                    std::numeric_limits<LONGLONG>::max()));
            if (StrFormatByteSizeW(static_cast<LONGLONG>(bytes),
                                   size_text.data(),
                                   static_cast<UINT>(size_text.size())) !=
                nullptr) {
                text += L'\t';
                text += size_text.data();
            }
        }
    }
    SetWindowTextW(status_bar(), text.c_str());
}

bool Pane::active() const noexcept {
    return host_ != nullptr && !host_->is_shutting_down();
}

bool Pane::register_window_class(HINSTANCE instance) noexcept {
    return register_simple_window_class(kWindowClassName, pane_window_proc,
                                        instance, nullptr,
                                        CS_HREDRAW | CS_VREDRAW);
}

void Pane::window_destroyed(HWND window) noexcept {
    if (window_ == window)
        window_ = nullptr;
}

bool Pane::create(HWND parent, int pane_index) noexcept {
    if (parent == nullptr || pane_index < 0)
        return false;
    destroy();
    index_ = static_cast<std::size_t>(pane_index);

    window_ =
        CreateWindowExW(0, kWindowClassName, nullptr,
                        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0,
                        0, parent, nullptr, GetModuleHandleW(nullptr), this);
    if (window_ == nullptr) {
        destroy();
        return false;
    }

    explorer_container_ = CreateWindowExW(
        0, L"STATIC", nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0,
        0, 0, 0, window_, nullptr, GetModuleHandleW(nullptr), nullptr);
    tab_strip_ui_.create(window_);

    constexpr std::array controls{PaneControl::back,      PaneControl::forward,
                                  PaneControl::up,        PaneControl::refresh,
                                  PaneControl::view_mode, PaneControl::pinned};
    const std::array<HWND *, 6> buttons{&back_button_,      &forward_button_,
                                        &up_button_,        &refresh_button_,
                                        &view_mode_button_, &pinned_button_};
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        *buttons[index] = CreateWindowExW(
            0, L"BUTTON", kButtonLabels[index],
            WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | BS_PUSHBUTTON |
                BS_OWNERDRAW,
            0, 0, 0, 0, window_,
            reinterpret_cast<HMENU>(encode_pane_control(controls[index])),
            GetModuleHandleW(nullptr), nullptr);
    }
    address_bar_ = CreateWindowExW(
        0, L"EDIT", nullptr,
        WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
        window_,
        reinterpret_cast<HMENU>(
            encode_pane_control(PaneControl::address_bar)),
        GetModuleHandleW(nullptr), nullptr);
    status_bar_ = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW, 0, 0, 0,
        0, window_, nullptr, GetModuleHandleW(nullptr), nullptr);
    folder_context_button_ = CreateWindowExW(
        0, L"BUTTON", L"Folder context menu",
        WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(
            encode_pane_control(PaneControl::folder_context)),
        GetModuleHandleW(nullptr), nullptr);

    const bool complete =
        window_ != nullptr && explorer_container_ != nullptr &&
        tab_strip() != nullptr && address_bar_ != nullptr &&
        status_bar_ != nullptr && back_button_ != nullptr &&
        forward_button_ != nullptr && up_button_ != nullptr &&
        refresh_button_ != nullptr && view_mode_button_ != nullptr &&
        pinned_button_ != nullptr && folder_context_button_ != nullptr;
    if (!complete ||
        !SetWindowSubclass(address_bar_, address_edit_proc, index_,
                           reinterpret_cast<DWORD_PTR>(this))) {
        destroy();
        return false;
    }
    for (HWND button : {back_button_, forward_button_, up_button_,
                        refresh_button_, view_mode_button_, pinned_button_,
                        folder_context_button_}) {
        if (!SetWindowSubclass(button, navigation_button_proc, 0, 0)) {
            destroy();
            return false;
        }
    }
    return true;
}

panedock::core::TabState *Pane::active_tab() const noexcept {
    if (bound_state_ == nullptr) return nullptr;
    for (auto &tab : bound_state_->tabs) {
        if (tab.id == bound_state_->active_tab_id) return &tab;
    }
    return nullptr;
}

Pane::NavigationGeneration Pane::begin_navigation() {
    if (pane_host() == nullptr) return 0;
    auto *tab = active_tab();
    if (tab == nullptr) return 0;
    auto &request = pending_navigation_;
    request.generation = explorer_host_.begin_navigation();
    request.group_id = pane_host()->active_group_id();
    request.tab_id = tab->id;
    // History policy belongs to this request, not the previous navigation.
    set_suppress_history(false);
    return request.generation;
}

HRESULT Pane::navigate_to(
    const panedock::core::ShellLocation &location) {
    if (pane_host() == nullptr) return E_UNEXPECTED;
    return explorer_host_.navigate(location, begin_navigation());
}

HRESULT Pane::navigate_up_one_level() {
    if (pane_host() == nullptr) return E_UNEXPECTED;
    return explorer_host_.navigate_up(begin_navigation());
}

bool Pane::navigation_request_is_current(
    NavigationGeneration generation) {
    if (pane_host() == nullptr) return false;
    auto *tab = active_tab();
    if (tab == nullptr) return false;

    if (generation < pending_navigation_.generation) return false;
    const auto &group_id = pane_host()->active_group_id();
    if (generation > pending_navigation_.generation) {
        pending_navigation_.generation = generation;
        pending_navigation_.group_id = group_id;
        pending_navigation_.tab_id = tab->id;
        set_suppress_history(false);
    }
    return panedock::core::navigation_request_matches(
        pending_navigation_, generation, group_id, tab->id);
}

void Pane::navigation_complete(
    NavigationGeneration generation,
    const panedock::core::ShellLocation &new_location) {
    if (!active()) return;
    if (!navigation_request_is_current(generation)) return;
    auto *tab = active_tab();
    if (tab == nullptr) return;
    auto completed_location = new_location;
    if (suppress_history_record_) {
        set_suppress_history(false);
        tab->location = std::move(completed_location);
        if (!tab->history.empty() && tab->history_index < tab->history.size())
            tab->history[tab->history_index] = tab->location;
    } else {
        panedock::core::record_navigation(*tab,
                                           std::move(completed_location));
    }
    apply_view_mode();
    if (!active()) return;
    apply_sort();
    if (!active()) return;
    tab_strip_ui().refresh();
    pane_host()->schedule_session_save();
}

void Pane::navigation_failed(NavigationGeneration generation) {
    if (pane_host() == nullptr) return;
    if (!navigation_request_is_current(generation)) return;
    // A pending back/forward navigation that fails asynchronously must still
    // release the suppression flag, or those buttons stay disabled forever.
    set_suppress_history(false);
    refresh_navigation_buttons();
}

void Pane::navigate_history(bool back) {
    if (!active()) return;
    auto *pane_state = bound_state_;
    if (pane_state == nullptr || suppress_history_record_) return;
    auto *tab = active_tab();
    if (tab == nullptr) return;
    const bool moved = back ? panedock::core::navigate_tab_back(*tab)
                            : panedock::core::navigate_tab_forward(*tab);
    if (!moved) return;
    const auto generation = begin_navigation();
    set_suppress_history(true);
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCall shell_call(pane_host());
        hr = explorer_host_.navigate(tab->location, generation);
    }
    if (!active()) return;
    if (FAILED(hr)) navigation_failed(generation);
    refresh_navigation_buttons();
}

void Pane::navigate_up() {
    if (!active()) return;
    if (bound_state_ == nullptr) return;
    {
        ShellCall shell_call(pane_host());
        (void)navigate_up_one_level();
    }
}

void Pane::refresh_view() {
    if (!active()) return;
    if (bound_state_ == nullptr) return;
    {
        ShellCall shell_call(pane_host());
        (void)explorer_host_.refresh();
    }
}

void Pane::capture_view_mode() {
    if (!active()) return;
    if (pane_state() == nullptr || !realized()) return;
    FOLDERVIEWMODE mode{};
    int image_size = -1;
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCall shell_call(pane_host());
        hr = explorer_host_.get_view_mode(mode, &image_size);
    }
    if (!active() || FAILED(hr)) return;
    auto *state = pane_state();
    if (state == nullptr) return;
    const std::string name = panedock::shell_core::view_mode_name(
        mode, image_size);
    if (auto *tab = active_tab(); tab != nullptr && !name.empty())
        tab->view_mode = name;
}

void Pane::capture_sort() {
    if (!active()) return;
    if (pane_state() == nullptr || !realized()) return;
    std::string column;
    bool ascending{};
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCall shell_call(pane_host());
        hr = explorer_host_.get_sort(column, ascending);
    }
    if (!active() || FAILED(hr)) return;
    if (auto *tab = active_tab(); tab != nullptr) {
        tab->sort_column = std::move(column);
        tab->sort_ascending = ascending;
    }
}

void Pane::apply_view_mode() {
    if (!active()) return;
    auto *tab = active_tab();
    if (tab == nullptr || !realized()) return;
    if (const auto selection = panedock::shell_core::parse_view_mode(
            tab->view_mode);
        selection.has_value()) {
        ShellCall shell_call(pane_host());
        (void)explorer_host_.set_view_mode(selection->mode,
                                           selection->image_size);
    } else if (tab->view_mode.empty()) {
        ShellCall shell_call(pane_host());
        (void)explorer_host_.set_view_mode(FVM_DETAILS);
    }
    if (!active()) return;
    capture_view_mode();
}

void Pane::apply_sort() {
    if (!active()) return;
    auto *tab = active_tab();
    if (tab == nullptr || !realized() || tab->sort_column.empty()) return;
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCall shell_call(pane_host());
        hr = explorer_host_.set_sort(tab->sort_column, tab->sort_ascending);
    }
    if (!active() || FAILED(hr)) return;
    capture_sort();
}

void Pane::capture_location() {
    if (pane_host() == nullptr ||
        pane_host()->location_capture_suppressed())
        return;
    if (pane_state() == nullptr || !realized() ||
        explorer_host_.location().parsing_name.empty())
        return;
    auto *tab = active_tab();
    if (tab == nullptr) return;
    tab->location = explorer_host_.location();
    capture_view_mode();
    if (!active()) return;
    capture_sort();
    tab = active_tab();
    if (!active() || tab == nullptr) return;
    if (!tab->history.empty() && tab->history_index < tab->history.size())
        tab->history[tab->history_index] = tab->location;
}

void Pane::set_view_mode(const panedock::shell_core::ViewModeOption &option) {
    if (!active()) return;
    if (pane_state() == nullptr) return;
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCall shell_call(pane_host());
        hr = explorer_host_.set_view_mode(option.selection.mode,
                                          option.selection.image_size);
    }
    if (!active() || FAILED(hr)) return;
    if (auto *tab = active_tab(); tab != nullptr) {
        tab->view_mode = panedock::shell_core::view_mode_name(
            option.selection.mode, option.selection.image_size);
        pane_host()->schedule_session_save();
    }
}

void Pane::show_view_mode_menu(POINT screen) {
    if (!active()) return;
    auto *tab = active_tab();
    if (tab == nullptr || window_ == nullptr) return;

    const auto current =
        panedock::shell_core::parse_view_mode(tab->view_mode);
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    const int menu_id_base =
        kViewModeMenuIdBase +
        static_cast<int>(index_ * panedock::shell_core::kViewModeOptions.size());
    int checked_id = 0;
    for (std::size_t index = 0;
         index < panedock::shell_core::kViewModeOptions.size(); ++index) {
        const auto &option = panedock::shell_core::kViewModeOptions[index];
        const bool checked =
            current.has_value() && current->mode == option.selection.mode &&
            current->image_size == option.selection.image_size;
        const int id = menu_id_base + static_cast<int>(index);
        AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(id), kViewModeLabels[index]);
        if (checked) checked_id = id;
    }
    if (checked_id != 0)
        CheckMenuRadioItem(
            menu, menu_id_base,
            menu_id_base +
                static_cast<int>(panedock::shell_core::kViewModeOptions.size()) -
                1,
            checked_id, MF_BYCOMMAND);
    SetForegroundWindow(window_);
    const int command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0,
        window_, nullptr);
    DestroyMenu(menu);
    if (!active() || command == 0) return;
    if (const HWND owner = GetParent(window_); owner != nullptr)
        SendMessageW(owner, WM_COMMAND, MAKEWPARAM(command, 0), 0);
}

void Pane::submit_address() {
    if (!active()) return;
    if (pane_state() == nullptr || address_bar_ == nullptr) return;
    const int length = GetWindowTextLengthW(address_bar_);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(address_bar_, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    const panedock::core::ShellLocation target{std::move(text), {}, {}};
    ShellCall shell_call(pane_host());
    (void)navigate_to(target);
}

void Pane::pin_current_folder() {
    if (!active()) return;
    if (pane_state() == nullptr) return;
    capture_location();
    if (!active()) return;
    if (const auto *tab = active_tab(); tab != nullptr)
        pane_host()->pin_location(tab->location);
}

void Pane::show_pinned_locations_menu(POINT screen) {
    if (!active()) return;
    if (pane_state() == nullptr || window_ == nullptr) return;

    const auto locations = pane_host()->pinned_locations();
    if (locations.size() < kPinnedMenuFixedLocationCount) return;
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    const int menu_id_base =
        kPinnedMenuIdBase + static_cast<int>(index_ * kPinnedMenuSlotsPerPane);
    for (std::size_t index = 0; index < kPinnedMenuFixedLocationCount;
         ++index) {
        AppendMenuW(menu, MF_STRING,
                    static_cast<UINT_PTR>(menu_id_base +
                                          static_cast<int>(index)),
                    locations[index].label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const std::size_t count = std::min(
        locations.size() - kPinnedMenuFixedLocationCount,
        static_cast<std::size_t>(kPinnedMenuMaxLocationCount));
    for (std::size_t index = 0; index < count; ++index) {
        AppendMenuW(
            menu, MF_STRING,
            static_cast<UINT_PTR>(menu_id_base + kPinnedMenuLocationOffset +
                                  static_cast<int>(index)),
            locations[kPinnedMenuFixedLocationCount + index].label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING,
                static_cast<UINT_PTR>(menu_id_base + kPinnedMenuAddOffset),
                L"Add Current Folder");
    AppendMenuW(
        menu, MF_STRING,
        static_cast<UINT_PTR>(menu_id_base + kPinnedMenuManageOffset),
        L"Manage Pinned Locations...");
    SetForegroundWindow(window_);
    const int command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, window_,
        nullptr);
    DestroyMenu(menu);
    if (!active() || command == 0) return;
    if (const HWND owner = GetParent(window_); owner != nullptr)
        SendMessageW(owner, WM_COMMAND, MAKEWPARAM(command, 0), 0);
}

void Pane::handle_tab_context_menu(POINT screen) {
    if (pane_state() == nullptr || window_ == nullptr) return;
    const auto item = tab_strip_ui_.tab_at_screen(screen);
    const auto &tabs = pane_state()->tabs;
    if (!item.has_value() || *item >= tabs.size()) return;
    const std::string tab_id = tabs[*item].id;
    const auto target_tab = tabs.begin() + static_cast<std::ptrdiff_t>(*item);
    const HWND window = GetParent(window_);
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCloseTabId),
                L"Close Tab");
    AppendMenuW(menu, MF_STRING | (tabs.size() == 1 ? MF_GRAYED : 0),
                static_cast<UINT_PTR>(kCloseOtherTabsId),
                L"Close Other Tabs");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCloseAllTabsId),
                L"Close All Tabs");
    AppendMenuW(menu,
                MF_STRING |
                    (target_tab + 1 == tabs.end() ? MF_GRAYED : 0),
                static_cast<UINT_PTR>(kCloseTabsToRightId),
                L"Close Tabs to the Right");
    SetForegroundWindow(window);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       screen.x, screen.y, 0, window,
                                       nullptr);
    DestroyMenu(menu);
    if (command == kCloseTabId) {
        close_tab(tab_id);
    } else if (command != 0) {
        close_tabs(tab_id, command);
    }
}

void Pane::finish_tab_change(bool navigate_active) {
    if (navigate_active && realized()) {
        auto *tab = active_tab();
        if (tab == nullptr) return;
        {
            ShellCall shell_call(pane_host());
            (void)navigate_to(tab->location);
        }
        if (!active()) return;
    }
    tab_strip_ui().refresh();
    pane_host()->schedule_session_save();
}

void Pane::switch_active_tab(const std::string &tab_id) {
    if (!active()) return;
    auto *pane_state = bound_state_;
    if (pane_state == nullptr) return;
    if (pane_state->active_tab_id == tab_id) {
        tab_strip_ui().refresh();
        return;
    }
    capture_location();
    pane_state = bound_state_;
    if (!active() || pane_state == nullptr) return;
    if (!panedock::core::set_active_tab(*pane_state, tab_id)) {
        tab_strip_ui().refresh();
        return;
    }
    finish_tab_change(true);
}

void Pane::cycle_active_tab(bool reverse) {
    if (!active()) return;
    auto *pane_state = bound_state_;
    if (pane_state == nullptr) return;
    const auto current = std::find_if(
        pane_state->tabs.begin(), pane_state->tabs.end(),
        [&](const auto &tab) { return tab.id == pane_state->active_tab_id; });
    if (current == pane_state->tabs.end()) return;
    const std::size_t index =
        static_cast<std::size_t>(current - pane_state->tabs.begin());
    const std::size_t next =
        reverse ? (index + pane_state->tabs.size() - 1) %
                     pane_state->tabs.size()
                : (index + 1) % pane_state->tabs.size();
    const std::string next_id = pane_state->tabs[next].id;
    switch_active_tab(next_id);
}

void Pane::add_tab(panedock::core::ShellLocation initial_location) {
    if (!active()) return;
    if (pane_host()->active_group_id().empty()) return;
    auto *pane_state = bound_state_;
    if (pane_state == nullptr) return;
    capture_location();
    pane_state = bound_state_;
    if (!active() || pane_state == nullptr) return;
    const std::string id = pane_host()->make_unique_tab_id();
    if (!panedock::core::add_tab(
            *pane_state,
            {id, std::move(initial_location), {}, {}, true, {}, 0}) ||
        !panedock::core::set_active_tab(*pane_state, id))
        return;
    finish_tab_change(true);
}

void Pane::activate_tab_at(std::size_t item) {
    const auto *pane_state = this->pane_state();
    if (pane_state == nullptr || item >= pane_state->tabs.size()) return;
    switch_active_tab(pane_state->tabs[item].id);
}

void Pane::add_default_tab() { add_tab({kDefaultTabParsingName, {}, {}}); }

void Pane::close_tab_at_screen(POINT screen) {
    const auto *pane_state = this->pane_state();
    if (pane_state == nullptr) return;
    const auto item = tab_strip_ui_.tab_at_screen(screen);
    if (!item.has_value() || *item >= pane_state->tabs.size()) return;
    close_tab(pane_state->tabs[*item].id);
}

void Pane::close_tab(const std::string &tab_id) {
    if (!active()) return;
    auto *pane_state = bound_state_;
    if (pane_state == nullptr) return;
    capture_location();
    pane_state = bound_state_;
    if (!active() || pane_state == nullptr) return;
    const bool closed_active = pane_state->active_tab_id == tab_id;
    if (!panedock::core::close_tab(
            *pane_state, tab_id,
            {kDefaultTabParsingName, {}, {}}))
        return;
    finish_tab_change(closed_active);
}

void Pane::close_tabs(const std::string &tab_id, int command) {
    if (!active()) return;
    if (pane_state() == nullptr) return;
    if (command != kCloseOtherTabsId && command != kCloseAllTabsId &&
        command != kCloseTabsToRightId) return;
    const auto& tabs = pane_state()->tabs;
    std::vector<std::string> tab_ids;
    if (command == kCloseOtherTabsId) {
        for (const auto& tab : tabs)
            if (tab.id != tab_id) tab_ids.push_back(tab.id);
    } else if (command == kCloseAllTabsId) {
        for (const auto& tab : tabs) tab_ids.push_back(tab.id);
    } else {
        const auto target_tab =
            std::find_if(tabs.begin(), tabs.end(), [&](const auto& tab) {
                return tab.id == tab_id;
            });
        if (target_tab != tabs.end()) {
            for (auto tab = target_tab + 1; tab != tabs.end(); ++tab)
                tab_ids.push_back(tab->id);
        }
    }
    for (const auto& id_to_close : tab_ids)
        close_tab(id_to_close);
}

void Pane::refresh_navigation_buttons() noexcept {
    const panedock::core::TabState *tab = active_tab();
    if (tab == nullptr) {
        EnableWindow(back_button_, FALSE);
        EnableWindow(forward_button_, FALSE);
        EnableWindow(up_button_, FALSE);
        EnableWindow(folder_context_button_, FALSE);
        return;
    }
    EnableWindow(back_button_, !suppress_history_record_ &&
                                   panedock::core::can_navigate_tab_back(*tab));
    EnableWindow(forward_button_,
                 !suppress_history_record_ &&
                     panedock::core::can_navigate_tab_forward(*tab));
    EnableWindow(up_button_, TRUE);
    EnableWindow(folder_context_button_, realized_);
}

void Pane::destroy() noexcept {
    // Fixed order, per design-spec.md §9.4 ("順序不可調換。view 存活期間
    // destroy parent HWND 是已知的崩潰面") and AGENTS.md ("Never destroy a
    // parent HWND while a view is alive"):
    //   1. RevokeDragDrop — the tab strip HWND it was registered on must
    //      outlive the revoke call.
    //   2. explorer_host_.destroy() — the live IExplorerBrowser (if any)
    //      must be torn down while its parent (explorer_container_) is
    //      still alive; ExplorerHost::destroy() is a no-op if never
    //      initialized.
    //   3. The remaining chrome child windows.
    //   4. explorer_container_, because the Shell view lived inside
    //      it — destroying it earlier is exactly the crash §9.4 warns
    //      about.
    //   5. pane HWND last. DestroyWindow would recursively destroy every
    //      child, including explorer_container_, so §9.4 requires both the
    //      browser and its container to be gone first.
    tab_strip_ui_.revoke_drag_hover_target();
    explorer_host_.destroy();
    realized_ = false;
    destroy_window(status_bar_);
    destroy_window(folder_context_button_);
    destroy_window(address_bar_);
    destroy_window(pinned_button_);
    destroy_window(view_mode_button_);
    destroy_window(refresh_button_);
    destroy_window(up_button_);
    destroy_window(forward_button_);
    destroy_window(back_button_);
    tab_strip_ui_.destroy();
    destroy_window(explorer_container_);
    destroy_window(window_);
    if (paint_dc_ != nullptr && paint_old_bitmap_ != nullptr)
        SelectObject(paint_dc_, paint_old_bitmap_);
    if (paint_bitmap_ != nullptr) DeleteObject(paint_bitmap_);
    if (paint_dc_ != nullptr) DeleteDC(paint_dc_);
    if (card_border_pen_ != nullptr) DeleteObject(card_border_pen_);
    paint_dc_ = nullptr;
    paint_bitmap_ = nullptr;
    paint_old_bitmap_ = nullptr;
    paint_size_ = {};
    card_border_pen_ = nullptr;
    card_border_pen_dpi_ = 0;
    laid_out_pane_rect_.reset();
    suppress_history_record_ = false;
    unbind();
}

HRESULT Pane::realize(const RECT &local_rect,
                      const panedock::core::ShellLocation &location) noexcept {
    const HRESULT hr =
        explorer_host_.initialize(explorer_container_, local_rect, location);
    realized_ = SUCCEEDED(hr);
    return hr;
}

void Pane::derealize() noexcept {
    explorer_host_.destroy();
    realized_ = false;
}

// PD-042: clip a pane's explorer container child window with rounded bottom
// corners while keeping the internal top edge square. Called whenever the
// container's rect changes (creation, WM_SIZE, WM_DPICHANGED — every
// apply_layout pass).
void Pane::apply_container_region(int width, int height, int radius) noexcept {
    const HWND container = explorer_container_;
    if (container == nullptr || width <= 0 || height <= 0) return;
    HRGN rounded = CreateRoundRectRgn(0, 0, width, height, radius, radius);
    if (rounded == nullptr) return;
    HRGN top_strip = CreateRectRgn(0, 0, width, radius);
    if (top_strip == nullptr) {
        DeleteObject(rounded);
        return;
    }
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (region == nullptr) {
        DeleteObject(top_strip);
        DeleteObject(rounded);
        return;
    }
    if (CombineRgn(region, rounded, top_strip, RGN_OR) == ERROR) {
        DeleteObject(region);
        DeleteObject(top_strip);
        DeleteObject(rounded);
        return;
    }
    DeleteObject(top_strip);
    DeleteObject(rounded);
    // Do not repaint this pane immediately. During a live layout pass every
    // changed pane gets its region here; apply_layout invalidates all of them
    // after the geometry commits have completed.
    if (SetWindowRgn(container, region, FALSE) == 0) {
        DeleteObject(region);
    }
}

void Pane::repaint_chrome() noexcept {
    if (window_ == nullptr) return;
    // Finish chrome painting before the next drag update, without a separate
    // child-background erase. Shell painting stays on its own schedule.
    RedrawWindow(window_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_NOCHILDREN |
                     RDW_UPDATENOW);
    const std::array<HWND, 10> chrome{
        tab_strip(),      address_bar_,      status_bar_,
        back_button_,     forward_button_,   up_button_,
        refresh_button_,  view_mode_button_, pinned_button_,
        folder_context_button_};
    for (HWND child : chrome) {
        if (child != nullptr)
            RedrawWindow(child, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
    }
}

bool Pane::set_rect(const RECT &rect) noexcept {
    // The pane's children have different sub-rectangles (tab, navigation,
    // footer and Shell container), so the app-shell layout pass owns their
    // parent-scoped batch. This method owns the committed outer-rect cache.
    const bool changed = !laid_out_pane_rect_.has_value() ||
                         !EqualRect(&laid_out_pane_rect_.value(), &rect);
    laid_out_pane_rect_ = rect;
    return changed;
}

void Pane::set_paint_geometry(const RECT &navigation_background,
                              const RECT &pane_window_rect, UINT dpi) noexcept {
    navigation_background_ = navigation_background;
    OffsetRect(&navigation_background_, -pane_window_rect.left,
               -pane_window_rect.top);
    paint_dpi_ = dpi;
}

void Pane::paint_background(HDC target) noexcept {
    if (target == nullptr || window_ == nullptr) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return;

    const int shadow_offset = pane_card_shadow_offset(paint_dpi_);
    const int surface_margin = pane_card_outset(paint_dpi_) + shadow_offset;
    const int surface_width = width + surface_margin;
    const int surface_height = height + surface_margin;

    if (paint_dc_ == nullptr) paint_dc_ = CreateCompatibleDC(target);
    if (paint_dc_ == nullptr) return;
    if (paint_bitmap_ == nullptr || paint_size_.cx != surface_width ||
        paint_size_.cy != surface_height) {
        HBITMAP bitmap =
            CreateCompatibleBitmap(target, surface_width, surface_height);
        if (bitmap == nullptr) return;
        if (paint_old_bitmap_ == nullptr)
            paint_old_bitmap_ = static_cast<HBITMAP>(
                SelectObject(paint_dc_, bitmap));
        else
            SelectObject(paint_dc_, bitmap);
        if (paint_bitmap_ != nullptr) DeleteObject(paint_bitmap_);
        paint_bitmap_ = bitmap;
        paint_size_ = {surface_width, surface_height};
    }

    const RECT surface{0, 0, surface_width, surface_height};
    SetDCBrushColor(paint_dc_, RGB(243, 246, 249));
    FillRect(paint_dc_, &surface,
             static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const int radius = pane_card_radius(paint_dpi_);
    const RECT card{client.left, client.top,
                    client.right - shadow_offset,
                    client.bottom - shadow_offset};
    RECT shadow = card;
    OffsetRect(&shadow, shadow_offset, shadow_offset);
    fill_rounded_rect(paint_dc_, shadow, radius, RGB(235, 239, 244), CLR_NONE);
    fill_rounded_rect(paint_dc_, card, radius, RGB(255, 255, 255), CLR_NONE);

    if (card_border_pen_ == nullptr || card_border_pen_dpi_ != paint_dpi_) {
        if (card_border_pen_ != nullptr) DeleteObject(card_border_pen_);
        card_border_pen_ = CreatePen(
            PS_SOLID,
            (std::max)(1, MulDiv(1, static_cast<int>(paint_dpi_), 96)),
            RGB(232, 237, 242));
        card_border_pen_dpi_ = paint_dpi_;
    }
    if (card_border_pen_ != nullptr) {
        const HGDIOBJ old_pen = SelectObject(paint_dc_, card_border_pen_);
        const HGDIOBJ old_brush =
            SelectObject(paint_dc_, GetStockObject(NULL_BRUSH));
        RoundRect(paint_dc_, card.left, card.top, card.right, card.bottom,
                  radius, radius);
        SelectObject(paint_dc_, old_brush);
        SelectObject(paint_dc_, old_pen);
    }

    if (navigation_background_.right > navigation_background_.left &&
        navigation_background_.bottom > navigation_background_.top) {
        const int navigation_radius = (std::max)(
            1, MulDiv(4, static_cast<int>(paint_dpi_), 96));
        fill_rounded_rect(paint_dc_, navigation_background_, navigation_radius,
                          RGB(251, 252, 253), RGB(217, 225, 234));
    }
    BitBlt(target, 0, 0, width, height, paint_dc_, 0, 0, SRCCOPY);
}

void Pane::set_visible(bool visible) noexcept {
    const int command = visible ? SW_SHOW : SW_HIDE;
    ShowWindow(window_, command);
    ShowWindow(explorer_container_, command);
    ShowWindow(tab_strip(), command);
    ShowWindow(back_button_, command);
    ShowWindow(forward_button_, command);
    ShowWindow(up_button_, command);
    ShowWindow(refresh_button_, command);
    ShowWindow(view_mode_button_, command);
    ShowWindow(pinned_button_, command);
    ShowWindow(address_bar_, command);
    ShowWindow(status_bar_, command);
    ShowWindow(folder_context_button_, command);
}

void Pane::apply_font(HFONT font) noexcept {
    set_font(tab_strip(), font);
    set_font(back_button_, font);
    set_font(forward_button_, font);
    set_font(up_button_, font);
    set_font(refresh_button_, font);
    set_font(view_mode_button_, font);
    set_font(pinned_button_, font);
    set_font(address_bar_, font);
    set_font(status_bar_, font);
    set_font(folder_context_button_, font);
}

} // namespace panedock::app_shell
