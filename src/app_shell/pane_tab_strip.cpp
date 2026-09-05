#include "app_shell/pane_tab_strip.h"

#include "app_shell/pane.h"
#include "app_shell/pane_host.h"
#include "sidebar/sidebar.h"

#include <commctrl.h>

namespace panedock::app_shell {
namespace {

// PD-049: content-sized tabs with a fixed add button at the right edge.
constexpr int kTabMinWidth = 72;
constexpr int kTabMaxWidth = 200;
constexpr int kTabAddButtonWidth = 36;
constexpr int kTabAddButtonHorizontalInset = 5;
constexpr int kTabAddButtonVerticalInset = 3;
// PD-073: reserved only while the tab content overflows its viewport. PD-107
// derives the final interactive/drawn rectangles from the visual geometry.
constexpr int kTabScrollButtonWidth = 20;
// PD-080: compact button dimensions and the user-confirmed pixel offsets.
constexpr int kTabScrollButtonVisualWidth = 18;
constexpr int kTabScrollButtonVisualHeight = 20;
constexpr int kTabScrollButtonVisualOffsetX = 6;
constexpr int kTabScrollButtonVisualOffsetY = 1;
constexpr int kTabScrollButtonCornerRadius = 4;
constexpr int kTabScrollButtonGlyphHalf = 5;
// PD-062: independent 96-DPI tab visual metrics. Gap is split across the
// two sides of each tab; text padding is inside the rounded tab; vertical
// padding is independent so the tab row can grow without changing either.
constexpr int kTabHorizontalGap = 6;
constexpr int kTabTextHorizontalPadding = 6;
constexpr int kTabVerticalPadding = 3;
constexpr int kTabCornerRadius = 6;
// Kept as layout reserve only; closing remains middle-click (PD-062 scope).
constexpr int kTabCloseButtonSpace = 16;
// PD-081: use a bold UI-font glyph so the add button keeps PD-062's larger,
// heavier visual weight without relying on two independently capped strokes.
constexpr int kTabPlusFontSize = 18;
// PD-076: keep tab active colors aligned with sidebar.cpp without introducing
// a cross-module palette; use a stronger neutral hover fill for tab contrast.
constexpr COLORREF kTabActiveBackground = RGB(234, 241, 255);
constexpr COLORREF kTabHoverBackground = RGB(226, 232, 240);
constexpr COLORREF kTabActiveText = RGB(23, 75, 180);
constexpr COLORREF kTabText = RGB(31, 41, 55);
constexpr COLORREF kTabActiveBorder = RGB(191, 211, 245);
constexpr COLORREF kTabBorder = RGB(232, 237, 242);
constexpr COLORREF kTabAddHoverBackground = RGB(236, 240, 244);
constexpr COLORREF kTabAddBorder = RGB(226, 232, 240);
constexpr COLORREF kTabAddGlyph = RGB(31, 41, 55);
constexpr int kActivePaneIndicatorHeight = 3;
constexpr UINT_PTR kTabAddTooltipIdBase = 1000;
constexpr UINT_PTR kTabScrollTooltipIdBase = 1010;

int scaled_value(HWND window, int value) noexcept {
    return std::max(1, MulDiv(value, static_cast<int>(GetDpiForWindow(window)),
                              96));
}

void draw_tab_scroll_button(HWND window, HDC dc, const RECT& rect,
                            bool forward, bool disabled,
                            bool hovered) noexcept {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    const panedock::app_shell::TabScrollButtonVisual visual{
        static_cast<int>(rect.left), static_cast<int>(rect.top),
        static_cast<int>(rect.right), static_cast<int>(rect.bottom)};
    const COLORREF background_color =
        !disabled && hovered ? RGB(236, 240, 244) : RGB(255, 255, 255);
    HBRUSH background = CreateSolidBrush(background_color);
    HPEN border = CreatePen(PS_SOLID, scaled_value(window, 1),
                            RGB(226, 232, 240));
    if (background != nullptr && border != nullptr) {
        const HGDIOBJ old_brush = SelectObject(dc, background);
        const HGDIOBJ old_pen = SelectObject(dc, border);
        const int radius = panedock::app_shell::tab_scroll_button_corner_radius(
            scaled_value(window, kTabScrollButtonCornerRadius),
            visual.width(), visual.height());
        RoundRect(dc, visual.left, visual.top, visual.right, visual.bottom,
                  radius, radius);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
    }
    if (background != nullptr) {
        DeleteObject(background);
    }
    if (border != nullptr) DeleteObject(border);
    const auto glyph = panedock::app_shell::tab_scroll_button_glyph(
        visual,
        panedock::app_shell::tab_scroll_button_glyph_half(
            scaled_value(window, kTabScrollButtonGlyphHalf), visual.width(),
            visual.height()),
        forward);
    const COLORREF color =
        disabled ? RGB(190, 197, 209) : RGB(90, 102, 122);
    HPEN pen = CreatePen(PS_SOLID, scaled_value(window, 1), color);
    if (pen == nullptr) return;
    const HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, glyph.start_x, glyph.start_y, nullptr);
    LineTo(dc, glyph.tip_x, glyph.tip_y);
    LineTo(dc, glyph.end_x, glyph.end_y);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

} // namespace

bool PaneTabStrip::create(HWND parent) noexcept {
    tab_strip_ = CreateWindowExW(
        0, L"STATIC", nullptr,
        WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | SS_NOTIFY, 0, 0, 0, 0,
        parent,
        reinterpret_cast<HMENU>(encode_pane_control(PaneControl::tab_strip)),
        GetModuleHandleW(nullptr), nullptr);
    return tab_strip_ != nullptr;
}

void PaneTabStrip::destroy() noexcept {
    revoke_drag_hover_target();
    if (tab_strip_ != nullptr) {
        DestroyWindow(tab_strip_);
        tab_strip_ = nullptr;
    }
    tab_tooltips_registered_ = false;
    tab_visuals_.clear();
    tab_strip_geometry_ = TabStripGeometry{};
    tab_hover_index_.reset();
    tab_scroll_hover_index_.reset();
}

void PaneTabStrip::apply_item_size(bool reveal_active) {
    if (tab_strip_ == nullptr || owner_->pane_host() == nullptr) return;
    auto &pane = *owner_;
    const HWND strip = tab_strip_;
    RECT client{};
    GetClientRect(strip, &client);
    const int min_width = scaled_value(strip, kTabMinWidth);
    const int max_width = scaled_value(strip, kTabMaxWidth);
    const int add_width = scaled_value(strip, kTabAddButtonWidth);
    const auto& visuals = tab_visuals();
    const int text_reserve = scaled_value(
        strip, 2 * kTabTextHorizontalPadding + kTabCloseButtonSpace);
    std::vector<int> preferred_widths;
    preferred_widths.reserve(visuals.size());
    HDC dc = GetDC(strip);
    const HFONT font = pane.pane_host()->chrome_font();
    HGDIOBJ previous = dc == nullptr ? nullptr : SelectObject(dc, font);
    for (const auto& visual : visuals) {
        SIZE size{};
        if (dc != nullptr)
            GetTextExtentPoint32W(dc, visual.text.c_str(),
                                  static_cast<int>(visual.text.size()), &size);
        preferred_widths.push_back(
            static_cast<int>(size.cx) + text_reserve);
    }
    if (dc != nullptr) {
        SelectObject(dc, previous);
        ReleaseDC(strip, dc);
    }
    const auto drag_layout = pane.pane_host()->tab_drag_layout(
        pane, strip, min_width, max_width, text_reserve);

    std::optional<std::size_t> active_index;
    if (reveal_active && pane.pane_state() != nullptr) {
        const auto& pane_state = *pane.pane_state();
        for (std::size_t index = 0; index < pane_state.tabs.size(); ++index) {
            if (pane_state.tabs[index].id == pane_state.active_tab_id) {
                active_index = index;
                break;
            }
        }
    }
    const std::span<const int> preferred_span(
        preferred_widths.data(), preferred_widths.size());
    const panedock::app_shell::TabStripLayoutInput layout_input{
        preferred_span,
        static_cast<int>(client.right - client.left),
        static_cast<int>(client.bottom - client.top),
        min_width,
        max_width,
        add_width,
        scaled_value(strip, kTabAddButtonHorizontalInset),
        scaled_value(strip, kTabAddButtonVerticalInset),
        scaled_value(strip, kTabScrollButtonWidth),
        scaled_value(strip, kTabScrollButtonVisualWidth),
        scaled_value(strip, kTabScrollButtonVisualHeight),
        scaled_value(strip, kTabScrollButtonVisualOffsetX),
        scaled_value(strip, kTabScrollButtonVisualOffsetY),
        tab_geometry().scroll_offset,
        drag_layout,
        active_index};
    set_geometry(panedock::app_shell::layout_tab_strip(layout_input));
    InvalidateRect(strip, nullptr, FALSE);
    update_tab_strip_tooltips(pane.pane_host()->tooltip());
}

void PaneTabStrip::refresh() {
    if (owner_->pane_host() == nullptr) return;
    auto &pane = *owner_;
    set_tab_hover(std::nullopt);
    set_scroll_hover(std::nullopt);
    if (pane.pane_state() == nullptr) {
        set_tabs({});
        apply_item_size();
        pane.pane_host()->refresh_navigation_chrome(pane);
        return;
    }

    auto* pane_state = pane.pane_state();
    std::vector<std::pair<std::string, std::wstring>> tabs;
    tabs.reserve(pane_state->tabs.size());
    for (const auto& tab : pane_state->tabs)
        tabs.emplace_back(tab.id, tab.location.parsing_name);
    std::vector<std::wstring> labels;
    labels.reserve(tabs.size());
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        labels.push_back(pane.pane_host()->tab_display_text(tabs[index].second));
        auto* current = pane.pane_state();
        if (current != pane_state || current->tabs.size() != tabs.size() ||
            current->tabs[index].id != tabs[index].first ||
            current->tabs[index].location.parsing_name != tabs[index].second)
            return;
    }
    set_tabs(labels);
    apply_item_size(true);
    pane.pane_host()->refresh_navigation_chrome(pane);
}

RECT PaneTabStrip::viewport_rect() const noexcept {
    return to_win32_rect(tab_geometry().viewport);
}

std::optional<std::size_t> PaneTabStrip::scroll_button_at(POINT point) const noexcept {
    return tab_scroll_button_hit_test(tab_geometry(), point.x, point.y);
}

int PaneTabStrip::scroll_step(bool forward) const noexcept {
    return tab_scroll_step(tab_geometry(), forward);
}

void PaneTabStrip::scroll(bool forward) {
    if (tab_strip_ == nullptr || owner_->pane_host() == nullptr) return;
    auto &chrome = *this;
    panedock::app_shell::TabStripGeometry geometry = chrome.tab_geometry();
    if (geometry.max_scroll_offset <= 0) return;
    const int offset = geometry.scroll_offset;
    const int maximum = geometry.max_scroll_offset;
    if ((!forward && offset <= 0) || (forward && offset >= maximum)) return;
    const int step = scroll_step(forward);
    if (step <= 0) return;
    geometry.scroll_offset = offset + (forward ? step : -step);
    chrome.set_geometry(std::move(geometry));
    apply_item_size();
}

void PaneTabStrip::update_tab_strip_tooltips(HWND tooltip) noexcept {
    if (tooltip == nullptr || tab_strip_ == nullptr) return;

    const std::array<RECT, 3> rects{
        to_win32_rect(tab_strip_geometry_.add_rect),
        to_win32_rect(tab_strip_geometry_.scroll_button_rects[0]),
        to_win32_rect(tab_strip_geometry_.scroll_button_rects[1])};
    const std::array<UINT_PTR, 3> ids{
        kTabAddTooltipIdBase + static_cast<UINT_PTR>(owner_->index()),
        kTabScrollTooltipIdBase + static_cast<UINT_PTR>(owner_->index() * 2),
        kTabScrollTooltipIdBase + static_cast<UINT_PTR>(owner_->index() * 2 + 1)};
    constexpr std::array<const wchar_t *, 3> texts{
        L"New tab", L"Scroll tabs left", L"Scroll tabs right"};
    const UINT message =
        tab_tooltips_registered_ ? TTM_NEWTOOLRECT : TTM_ADDTOOLW;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        TOOLINFOW info{};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_SUBCLASS;
        info.hwnd = tab_strip_;
        info.uId = ids[index];
        info.rect = rects[index];
        info.lpszText = const_cast<wchar_t *>(texts[index]);
        SendMessageW(tooltip, message, 0, reinterpret_cast<LPARAM>(&info));
    }
    tab_tooltips_registered_ = true;
}

void PaneTabStrip::set_tabs(std::span<const std::wstring> labels) noexcept {
    tab_visuals_.clear();
    tab_visuals_.reserve(labels.size());
    for (const auto &label : labels)
        tab_visuals_.push_back({label});
}

std::optional<std::size_t> PaneTabStrip::tab_at_screen(POINT screen) const noexcept {
    if (tab_strip_ == nullptr)
        return std::nullopt;
    POINT client = screen;
    ScreenToClient(tab_strip_, &client);
    return tab_at(client);
}

bool PaneTabStrip::register_drag_hover_target(IDropTarget *target) noexcept {
    if (tab_strip_ == nullptr || target == nullptr)
        return false;
    if (FAILED(RegisterDragDrop(tab_strip_, target)))
        return false;
    tab_drag_target_ = target;
    return true;
}

void PaneTabStrip::revoke_drag_hover_target() noexcept {
    if (tab_drag_target_ == nullptr)
        return;
    if (tab_strip_ != nullptr)
        RevokeDragDrop(tab_strip_);
    tab_drag_target_.Reset();
}

void PaneTabStrip::paint_contents(HDC dc, const TabStripPaintState &state) noexcept {
    const HWND window = tab_strip_;
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
    auto* pane_state = owner_->pane_state();
    if (pane_state == nullptr) return;
    const auto& pane = *pane_state;
    const auto& visuals = tab_visuals();
    const int horizontal_gap = scaled_value(window, kTabHorizontalGap);
    const int left_gap = horizontal_gap / 2;
    const int right_gap = horizontal_gap - left_gap;
    const int text_padding =
        scaled_value(window, kTabTextHorizontalPadding);
    const int vertical_padding = scaled_value(window, kTabVerticalPadding);
    const int radius = scaled_value(window, kTabCornerRadius);
    const int border_width = scaled_value(window, 1);
    const HFONT font = owner_->pane_host() == nullptr
                           ? nullptr : owner_->pane_host()->chrome_font();
    const HGDIOBJ old_font = font != nullptr
                                 ? SelectObject(dc, font)
                                 : nullptr;
    SetBkMode(dc, TRANSPARENT);
    const RECT viewport = viewport_rect();
    const int saved_dc = SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right,
                      viewport.bottom);
    for (std::size_t index = 0; index < visuals.size(); ++index) {
        if (state.dragged_index == index) continue;
        RECT rect = to_win32_rect(
            tab_geometry().tab_rects[index]);
        rect.left = std::min(rect.right, rect.left + left_gap);
        rect.right = std::max(rect.left, rect.right - right_gap);
        rect.top = std::min(rect.bottom, rect.top + vertical_padding);
        rect.bottom = std::max(rect.top, rect.bottom - vertical_padding);
        const bool active = pane.tabs[index].id == pane.active_tab_id;
        const bool hovered = !active &&
                             tab_hover_index() == index;
        if (rect.right > rect.left && rect.bottom > rect.top) {
            const COLORREF fill_color =
                active ? kTabActiveBackground
                       : hovered ? kTabHoverBackground : RGB(244, 246, 248);
            const COLORREF border_color =
                active ? kTabActiveBorder : kTabBorder;
            HBRUSH fill = CreateSolidBrush(fill_color);
            HPEN border = CreatePen(PS_SOLID, border_width, border_color);
            if (fill != nullptr && border != nullptr) {
                const HGDIOBJ old_brush = SelectObject(dc, fill);
                const HGDIOBJ old_pen = SelectObject(dc, border);
                RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
                          radius, radius);
                SelectObject(dc, old_pen);
                SelectObject(dc, old_brush);
            }
            if (fill != nullptr) DeleteObject(fill);
            if (border != nullptr) DeleteObject(border);
        }
        RECT text_rect = rect;
        text_rect.left = std::min(text_rect.right,
                                  text_rect.left + text_padding);
        text_rect.right = std::max(text_rect.left,
                                   text_rect.right - text_padding);
        SetTextColor(dc, active ? kTabActiveText : kTabText);
        DrawTextW(dc, visuals[index].text.c_str(), -1, &text_rect,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    const auto& geometry = tab_geometry();
    if (geometry.placeholder_rect.has_value()) {
        RECT rect = to_win32_rect(*geometry.placeholder_rect);
        rect.left = std::min(rect.right, rect.left + left_gap);
        rect.right = std::max(rect.left, rect.right - right_gap);
        rect.top = std::min(rect.bottom, rect.top + vertical_padding);
        rect.bottom = std::max(rect.top, rect.bottom - vertical_padding);
        HBRUSH fill = CreateSolidBrush(RGB(238, 242, 246));
        HPEN border = CreatePen(PS_DOT, border_width, RGB(203, 213, 225));
        if (fill != nullptr && border != nullptr) {
            const HGDIOBJ old_brush = SelectObject(dc, fill);
            const HGDIOBJ old_pen = SelectObject(dc, border);
            RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius,
                      radius);
            SelectObject(dc, old_pen);
            SelectObject(dc, old_brush);
        }
        if (fill != nullptr) DeleteObject(fill);
        if (border != nullptr) DeleteObject(border);
        if (state.placeholder_text.has_value()) {
            RECT text_rect = rect;
            text_rect.left = std::min(text_rect.right,
                                      text_rect.left + text_padding);
            text_rect.right = std::max(text_rect.left,
                                       text_rect.right - text_padding);
            SetTextColor(dc, panedock::sidebar::kPlaceholderContent);
            DrawTextW(dc, state.placeholder_text->data(), -1, &text_rect,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
                          DT_NOPREFIX);
        }
    }
    if (saved_dc != 0) RestoreDC(dc, saved_dc);
    const auto& scroll_buttons = geometry.scroll_button_rects;
    if (scroll_buttons[0].right > scroll_buttons[0].left) {
        draw_tab_scroll_button(
            window, dc, to_win32_rect(scroll_buttons[0]), false,
            geometry.scroll_offset <= 0,
            scroll_hover_index() == 0);
        draw_tab_scroll_button(
            window, dc, to_win32_rect(scroll_buttons[1]), true,
            geometry.scroll_offset >= geometry.max_scroll_offset,
            scroll_hover_index() == 1);
    }
    const RECT add = to_win32_rect(geometry.add_rect);
    if (tab_hover_index().has_value() &&
        *tab_hover_index() == pane.tabs.size() &&
        add.right > add.left && add.bottom > add.top) {
        HBRUSH background = CreateSolidBrush(kTabAddHoverBackground);
        HPEN border = CreatePen(PS_SOLID, border_width, kTabAddBorder);
        if (background != nullptr && border != nullptr) {
            const HGDIOBJ old_brush = SelectObject(dc, background);
            const HGDIOBJ old_pen = SelectObject(dc, border);
            RoundRect(dc, add.left, add.top, add.right, add.bottom, radius,
                      radius);
            SelectObject(dc, old_pen);
            SelectObject(dc, old_brush);
        }
        if (background != nullptr) DeleteObject(background);
        if (border != nullptr) DeleteObject(border);
    }
    if (add.right > add.left && add.bottom > add.top) {
        HFONT plus_font = nullptr;
        if (font != nullptr) {
            LOGFONTW logfont{};
            if (GetObjectW(font, sizeof(logfont), &logfont) ==
                sizeof(logfont)) {
                logfont.lfHeight = -scaled_value(window, kTabPlusFontSize);
                logfont.lfWeight = FW_BOLD;
                plus_font = CreateFontIndirectW(&logfont);
            }
        }
        const HGDIOBJ old_plus_font =
            plus_font != nullptr ? SelectObject(dc, plus_font) : nullptr;
        SetTextColor(dc, kTabAddGlyph);
        RECT plus_rect = add;
        OffsetRect(&plus_rect, 0, -scaled_value(window, 2));
        DrawTextW(dc, L"+", 1, &plus_rect,
                  DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        if (old_plus_font != nullptr) SelectObject(dc, old_plus_font);
        if (plus_font != nullptr) DeleteObject(plus_font);
    }
    if (state.active_pane) {
        const int indicator_height = std::min(
            static_cast<int>(client.bottom),
            scaled_value(window, kActivePaneIndicatorHeight));
        if (indicator_height > 0) {
            const RECT indicator{client.left, client.top, client.right,
                                 client.top + indicator_height};
            HBRUSH brush = CreateSolidBrush(RGB(37, 99, 235));
            if (brush != nullptr) {
                FillRect(dc, &indicator, brush);
                DeleteObject(brush);
            }
        }
    }
    if (old_font != nullptr) SelectObject(dc, old_font);
}

void PaneTabStrip::paint(const TabStripPaintState &state) noexcept {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(tab_strip_, &paint);
    paint_contents(dc, state);
    EndPaint(tab_strip_, &paint);
}

void PaneTabStrip::mouse_move(POINT point) noexcept {
    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, tab_strip_, 0};
    TrackMouseEvent(&tracking);
    std::optional<std::size_t> hover =
        owner_->pane_state() == nullptr ? std::nullopt : tab_at(point);
    const RECT add = to_win32_rect(tab_geometry().add_rect);
    if (!hover.has_value() && owner_->pane_state() != nullptr &&
        PtInRect(&add, point)) {
        hover = owner_->pane_state()->tabs.size();
    }
    const bool tab_hover_changed = tab_hover_index() != hover;
    if (tab_hover_changed) set_tab_hover(hover);
    const auto scroll_hover = scroll_button_at(point);
    const bool scroll_hover_changed = scroll_hover_index() != scroll_hover;
    if (scroll_hover_changed) set_scroll_hover(scroll_hover);
    if (tab_hover_changed || scroll_hover_changed)
        InvalidateRect(tab_strip_, nullptr, FALSE);
}

void PaneTabStrip::mouse_leave() noexcept {
    const bool hover_changed = tab_hover_index().has_value() ||
                               scroll_hover_index().has_value();
    set_tab_hover(std::nullopt);
    set_scroll_hover(std::nullopt);
    if (hover_changed) InvalidateRect(tab_strip_, nullptr, FALSE);
}

std::optional<LRESULT> PaneTabStrip::handle_message(UINT message, WPARAM wparam,
                                                   LPARAM lparam) {
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_MOUSELEAVE) {
        mouse_leave();
        return 0;
    }
    if (owner_->pane_state() == nullptr) return std::nullopt;
    if (message == WM_MOUSEWHEEL) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        if (delta != 0) scroll(delta < 0);
        return 0;
    }
    const POINT point{static_cast<short>(LOWORD(lparam)),
                      static_cast<short>(HIWORD(lparam))};
    if (message == WM_LBUTTONDOWN) {
        if (const auto button = scroll_button_at(point)) {
            scroll(*button == 1);
            return 0;
        }
    }
    if (message == WM_LBUTTONDBLCLK) {
        const RECT add = to_win32_rect(tab_geometry().add_rect);
        if (!scroll_button_at(point) && !tab_at(point) &&
            !PtInRect(&add, point)) {
            SendMessageW(GetParent(GetParent(tab_strip_)),
                         kTabStripSelectionMessage,
                         static_cast<WPARAM>(owner_->index()), -1);
            return 0;
        }
    }
    return std::nullopt;
}

} // namespace panedock::app_shell
