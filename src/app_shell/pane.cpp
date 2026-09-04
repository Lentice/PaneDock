#include "app_shell/pane.h"

#include "app_shell/pane_message_dispatch.h"
#include "app_shell/window_helpers.h"

#include <commctrl.h>

#include <array>

namespace panedock::app_shell {
namespace {

constexpr std::array<const wchar_t *, 6> kButtonLabels{
    L"<", L">", L"Up", L"Refresh", L"View", L"Pinned"};
constexpr wchar_t kWindowClassName[] = L"PaneDock.Pane";

// Tooltip ids for this pane's tab-strip buttons on the shared tooltip
// control. Moved here with update_tab_strip_tooltips (PD-190).
constexpr UINT_PTR kTabAddTooltipIdBase = 1000;
constexpr UINT_PTR kTabScrollTooltipIdBase = 1010;

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

void set_font(HWND window, HFONT font) noexcept {
    if (window != nullptr && font != nullptr)
        SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
        const auto handled = handle_pane_control_message(
            window, pane->index(), message, wparam, lparam);
        if (handled.has_value()) return *handled;
        break;
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
    tab_strip_ = CreateWindowExW(
        0, L"STATIC", nullptr,
        WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | SS_NOTIFY, 0, 0, 0, 0,
        window_,
        reinterpret_cast<HMENU>(encode_pane_control(PaneControl::tab_strip)),
        GetModuleHandleW(nullptr), nullptr);

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
        tab_strip_ != nullptr && address_bar_ != nullptr &&
        status_bar_ != nullptr && back_button_ != nullptr &&
        forward_button_ != nullptr && up_button_ != nullptr &&
        refresh_button_ != nullptr && view_mode_button_ != nullptr &&
        pinned_button_ != nullptr && folder_context_button_ != nullptr;
    if (!complete) {
        destroy();
        return false;
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

void Pane::update_tab_strip_tooltips(HWND tooltip) noexcept {
    if (tooltip == nullptr || tab_strip_ == nullptr) return;

    const std::array<RECT, 3> rects{
        to_win32_rect(tab_strip_geometry_.add_rect),
        to_win32_rect(tab_strip_geometry_.scroll_button_rects[0]),
        to_win32_rect(tab_strip_geometry_.scroll_button_rects[1])};
    const std::array<UINT_PTR, 3> ids{
        kTabAddTooltipIdBase + static_cast<UINT_PTR>(index_),
        kTabScrollTooltipIdBase + static_cast<UINT_PTR>(index_ * 2),
        kTabScrollTooltipIdBase + static_cast<UINT_PTR>(index_ * 2 + 1)};
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
    revoke_drag_hover_target();
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
    destroy_window(tab_strip_);
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
    tab_tooltips_registered_ = false;
    tab_visuals_.clear();
    tab_strip_geometry_ = TabStripGeometry{};
    tab_hover_index_.reset();
    tab_scroll_hover_index_.reset();
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
    ShowWindow(tab_strip_, command);
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
    set_font(tab_strip_, font);
    set_font(back_button_, font);
    set_font(forward_button_, font);
    set_font(up_button_, font);
    set_font(refresh_button_, font);
    set_font(view_mode_button_, font);
    set_font(pinned_button_, font);
    set_font(address_bar_, font);
    set_font(status_bar_, font);
}

void Pane::set_tabs(std::span<const std::wstring> labels) noexcept {
    tab_visuals_.clear();
    tab_visuals_.reserve(labels.size());
    for (const auto &label : labels)
        tab_visuals_.push_back({label});
}

std::optional<std::size_t> Pane::tab_at_screen(POINT screen) const noexcept {
    if (tab_strip_ == nullptr)
        return std::nullopt;
    POINT client = screen;
    ScreenToClient(tab_strip_, &client);
    return tab_at(client);
}

bool Pane::register_drag_hover_target(IDropTarget *target) noexcept {
    if (tab_strip_ == nullptr || target == nullptr)
        return false;
    if (FAILED(RegisterDragDrop(tab_strip_, target)))
        return false;
    tab_drag_target_ = target;
    return true;
}

void Pane::revoke_drag_hover_target() noexcept {
    if (tab_drag_target_ == nullptr)
        return;
    if (tab_strip_ != nullptr)
        RevokeDragDrop(tab_strip_);
    tab_drag_target_.Reset();
}

} // namespace panedock::app_shell
