#include "app_shell/pane.h"

#include <array>

namespace panedock::app_shell {
namespace {

constexpr std::array<const wchar_t*, 6> kButtonLabels{
    L"<", L">", L"Up", L"Refresh", L"View", L"Pinned"};

void set_font(HWND window, HFONT font) noexcept {
    if (window != nullptr && font != nullptr)
        SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void destroy_window(HWND& window) noexcept {
    if (window != nullptr) {
        DestroyWindow(window);
        window = nullptr;
    }
}

}  // namespace

bool Pane::create(HWND parent, int pane_index) noexcept {
    if (parent == nullptr || pane_index < 0) return false;
    destroy();

    explorer_container_ = CreateWindowExW(
        0, L"STATIC", nullptr,
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    tab_strip_ = CreateWindowExW(
        0, L"STATIC", nullptr,
        WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | SS_NOTIFY, 0, 0, 0, 0,
        parent, reinterpret_cast<HMENU>(encode_pane_control(
                    PaneControl::tab_strip, static_cast<std::size_t>(pane_index))),
        GetModuleHandleW(nullptr), nullptr);

    constexpr std::array controls{
        PaneControl::back, PaneControl::forward, PaneControl::up,
        PaneControl::refresh, PaneControl::view_mode, PaneControl::pinned};
    const std::array<HWND*, 6> buttons{
        &back_button_, &forward_button_, &up_button_, &refresh_button_,
        &view_mode_button_, &pinned_button_};
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        *buttons[index] = CreateWindowExW(
            0, L"BUTTON", kButtonLabels[index],
            WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | BS_PUSHBUTTON |
                BS_OWNERDRAW,
            0, 0, 0, 0, parent,
            reinterpret_cast<HMENU>(encode_pane_control(
                controls[index], static_cast<std::size_t>(pane_index))),
            GetModuleHandleW(nullptr), nullptr);
    }
    address_bar_ = CreateWindowExW(
        0, L"EDIT", nullptr,
        WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
        parent, reinterpret_cast<HMENU>(encode_pane_control(
                    PaneControl::address_bar,
                    static_cast<std::size_t>(pane_index))),
        GetModuleHandleW(nullptr), nullptr);
    status_bar_ = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW, 0, 0,
        0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    const bool complete = explorer_container_ != nullptr &&
                          tab_strip_ != nullptr && address_bar_ != nullptr &&
                          status_bar_ != nullptr && back_button_ != nullptr &&
                          forward_button_ != nullptr && up_button_ != nullptr &&
                          refresh_button_ != nullptr &&
                          view_mode_button_ != nullptr &&
                          pinned_button_ != nullptr;
    if (!complete) {
        destroy();
        return false;
    }
    return true;
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
    //   4. explorer_container_ last, because the Shell view lived inside
    //      it — destroying it earlier is exactly the crash §9.4 warns
    //      about.
    revoke_drag_hover_target();
    explorer_host_.destroy();
    realized_ = false;
    destroy_window(status_bar_);
    destroy_window(address_bar_);
    destroy_window(pinned_button_);
    destroy_window(view_mode_button_);
    destroy_window(refresh_button_);
    destroy_window(up_button_);
    destroy_window(forward_button_);
    destroy_window(back_button_);
    destroy_window(tab_strip_);
    destroy_window(explorer_container_);
    laid_out_pane_rect_.reset();
    tab_tooltips_registered_ = false;
    tab_visuals_.clear();
    tab_strip_geometry_ = TabStripGeometry{};
    tab_hover_index_.reset();
    tab_scroll_hover_index_.reset();
    suppress_history_record_ = false;
}

HRESULT Pane::realize(const RECT& local_rect,
                      const panedock::core::ShellLocation& location) noexcept {
    const HRESULT hr = explorer_host_.initialize(
        explorer_container_, local_rect, location);
    realized_ = SUCCEEDED(hr);
    return hr;
}

void Pane::derealize() noexcept {
    explorer_host_.destroy();
    realized_ = false;
}

bool Pane::set_rect(const RECT& rect, HDWP* deferred) noexcept {
    // The pane's children have different sub-rectangles (tab, navigation,
    // footer and Shell container), so the app-shell layout pass owns their
    // parent-scoped batch. This method owns the committed outer-rect cache.
    (void)deferred;
    const bool changed = !laid_out_pane_rect_.has_value() ||
                         !EqualRect(&laid_out_pane_rect_.value(), &rect);
    laid_out_pane_rect_ = rect;
    return changed;
}

void Pane::set_visible(bool visible) noexcept {
    const int command = visible ? SW_SHOW : SW_HIDE;
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
    for (const auto& label : labels) tab_visuals_.push_back({label});
}

std::optional<std::size_t> Pane::tab_at_screen(POINT screen) const noexcept {
    if (tab_strip_ == nullptr) return std::nullopt;
    POINT client = screen;
    ScreenToClient(tab_strip_, &client);
    return tab_at(client);
}

bool Pane::register_drag_hover_target(IDropTarget* target) noexcept {
    if (tab_strip_ == nullptr || target == nullptr) return false;
    if (FAILED(RegisterDragDrop(tab_strip_, target))) return false;
    tab_drag_target_ = target;
    return true;
}

void Pane::revoke_drag_hover_target() noexcept {
    if (tab_drag_target_ == nullptr) return;
    if (tab_strip_ != nullptr) RevokeDragDrop(tab_strip_);
    tab_drag_target_.Reset();
}

}  // namespace panedock::app_shell
