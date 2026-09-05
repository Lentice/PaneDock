#include "app_shell/pane.h"

#include "app_shell/pane_message_dispatch.h"
#include "app_shell/pane_host.h"
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
constexpr std::array<const wchar_t *, 8> kViewModeLabels{
    L"Extra large icons", L"Large icons", L"Medium icons", L"Small icons",
    L"List", L"Details", L"Tiles", L"Content"};

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

Pane::NavigationGeneration Pane::begin_navigation() {
    if (pane_host() == nullptr) return 0;
    auto *tab = active_tab();
    if (tab == nullptr) return 0;
    auto &request = pending_navigation_;
    request.generation = explorer_host_.begin_navigation();
    request.group_id = pane_host()->active_group_id();
    request.tab_id = tab->id;
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
    }
    return panedock::core::navigation_request_matches(
        pending_navigation_, generation, group_id, tab->id);
}

void Pane::record_navigation_result(
    const panedock::core::ShellLocation &new_location) {
    if (pane_host() == nullptr) return;
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
    if (pane_host() == nullptr) return;
    if (pane_host()->is_shutting_down()) return;
    auto *pane_state = bound_state_;
    if (pane_state == nullptr || suppress_history_record_) return;
    auto *tab = active_tab();
    if (tab == nullptr) return;
    const bool moved = back ? panedock::core::navigate_tab_back(*tab)
                            : panedock::core::navigate_tab_forward(*tab);
    if (!moved) return;
    set_suppress_history(true);
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCall shell_call(pane_host());
        hr = navigate_to(tab->location);
    }
    if (pane_host()->is_shutting_down()) return;
    if (FAILED(hr)) set_suppress_history(false);
    refresh_navigation_buttons();
}

void Pane::navigate_up() {
    if (pane_host() == nullptr) return;
    if (pane_host()->is_shutting_down()) return;
    if (bound_state_ == nullptr) return;
    {
        ShellCall shell_call(pane_host());
        (void)navigate_up_one_level();
    }
}

void Pane::refresh_view() {
    if (pane_host() == nullptr) return;
    if (pane_host()->is_shutting_down()) return;
    if (bound_state_ == nullptr) return;
    {
        ShellCall shell_call(pane_host());
        (void)explorer_host_.refresh(begin_navigation());
    }
}

void Pane::capture_view_mode() {
    if (pane_host() == nullptr || pane_host()->is_shutting_down()) return;
    if (pane_state() == nullptr || !realized()) return;
    FOLDERVIEWMODE mode{};
    int image_size = -1;
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCall shell_call(pane_host());
        hr = explorer_host_.get_view_mode(mode, &image_size);
    }
    if (pane_host()->is_shutting_down() || FAILED(hr)) return;
    auto *state = pane_state();
    if (state == nullptr) return;
    const std::string name = panedock::shell_core::view_mode_name(
        mode, image_size);
    if (auto *tab = active_tab(); tab != nullptr && !name.empty())
        tab->view_mode = name;
}

void Pane::capture_sort() {
    if (pane_host() == nullptr || pane_host()->is_shutting_down()) return;
    if (pane_state() == nullptr || !realized()) return;
    std::string column;
    bool ascending{};
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCall shell_call(pane_host());
        hr = explorer_host_.get_sort(column, ascending);
    }
    if (pane_host()->is_shutting_down() || FAILED(hr)) return;
    if (auto *tab = active_tab(); tab != nullptr) {
        tab->sort_column = std::move(column);
        tab->sort_ascending = ascending;
    }
}

void Pane::apply_view_mode() {
    if (pane_host() == nullptr || pane_host()->is_shutting_down()) return;
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
    if (pane_host()->is_shutting_down()) return;
    capture_view_mode();
}

void Pane::apply_sort() {
    if (pane_host() == nullptr || pane_host()->is_shutting_down()) return;
    auto *tab = active_tab();
    if (tab == nullptr || !realized() || tab->sort_column.empty()) return;
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCall shell_call(pane_host());
        hr = explorer_host_.set_sort(tab->sort_column, tab->sort_ascending);
    }
    if (pane_host()->is_shutting_down() || FAILED(hr)) return;
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
    if (pane_host()->is_shutting_down()) return;
    capture_sort();
    tab = active_tab();
    if (pane_host()->is_shutting_down() || tab == nullptr) return;
    if (!tab->history.empty() && tab->history_index < tab->history.size())
        tab->history[tab->history_index] = tab->location;
}

void Pane::set_view_mode(const panedock::shell_core::ViewModeOption &option) {
    if (pane_host() == nullptr || pane_host()->is_shutting_down()) return;
    if (pane_state() == nullptr) return;
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCall shell_call(pane_host());
        hr = explorer_host_.set_view_mode(option.selection.mode,
                                          option.selection.image_size);
    }
    if (pane_host()->is_shutting_down() || FAILED(hr)) return;
    if (auto *tab = active_tab(); tab != nullptr) {
        tab->view_mode = panedock::shell_core::view_mode_name(
            option.selection.mode, option.selection.image_size);
        pane_host()->schedule_session_save();
    }
}

void Pane::show_view_mode_menu(POINT screen) {
    if (pane_host() == nullptr || pane_host()->is_shutting_down()) return;
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
    if (pane_host()->is_shutting_down() || command == 0) return;
    if (const HWND owner = GetParent(window_); owner != nullptr)
        SendMessageW(owner, WM_COMMAND, MAKEWPARAM(command, 0), 0);
}

void Pane::submit_address() {
    if (pane_host() == nullptr || pane_host()->is_shutting_down()) return;
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
    if (pane_host() == nullptr || pane_host()->is_shutting_down()) return;
    if (pane_state() == nullptr) return;
    capture_location();
    if (pane_host()->is_shutting_down()) return;
    if (const auto *tab = active_tab(); tab != nullptr)
        pane_host()->pin_location(tab->location);
}

void Pane::show_pinned_locations_menu(POINT screen) {
    if (pane_host() == nullptr || pane_host()->is_shutting_down()) return;
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
    if (pane_host()->is_shutting_down() || command == 0) return;
    if (const HWND owner = GetParent(window_); owner != nullptr)
        SendMessageW(owner, WM_COMMAND, MAKEWPARAM(command, 0), 0);
}

void Pane::switch_active_tab(const std::string &tab_id) {
    if (pane_host() == nullptr) return;
    if (pane_host()->is_shutting_down()) return;
    auto *pane_state = bound_state_;
    if (pane_state == nullptr) return;
    if (pane_state->active_tab_id == tab_id) {
        pane_host()->tab_strip_needs_refresh(*this);
        return;
    }
    capture_location();
    pane_state = bound_state_;
    if (pane_host()->is_shutting_down() || pane_state == nullptr) return;
    if (!panedock::core::set_active_tab(*pane_state, tab_id)) {
        pane_host()->tab_strip_needs_refresh(*this);
        return;
    }
    if (realized()) {
        auto *tab = active_tab();
        if (tab == nullptr) return;
        {
            ShellCall shell_call(pane_host());
            (void)navigate_to(tab->location);
        }
        if (pane_host()->is_shutting_down()) return;
    }
    pane_host()->tab_strip_needs_refresh(*this);
    pane_host()->schedule_session_save();
}

void Pane::cycle_active_tab(bool reverse) {
    if (pane_host() == nullptr) return;
    if (pane_host()->is_shutting_down()) return;
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
    if (pane_host() == nullptr) return;
    if (pane_host()->is_shutting_down()) return;
    if (pane_host()->active_group_id().empty()) return;
    auto *pane_state = bound_state_;
    if (pane_state == nullptr) return;
    capture_location();
    pane_state = bound_state_;
    if (pane_host()->is_shutting_down() || pane_state == nullptr) return;
    const std::string id = pane_host()->make_unique_tab_id();
    if (!panedock::core::add_tab(
            *pane_state,
            {id, std::move(initial_location), {}, {}, true, {}, 0}) ||
        !panedock::core::set_active_tab(*pane_state, id))
        return;
    if (realized()) {
        auto *tab = active_tab();
        if (tab == nullptr) return;
        {
            ShellCall shell_call(pane_host());
            (void)navigate_to(tab->location);
        }
        if (pane_host()->is_shutting_down()) return;
    }
    pane_host()->tab_strip_needs_refresh(*this);
    pane_host()->schedule_session_save();
}

void Pane::close_tab(const std::string &tab_id) {
    if (pane_host() == nullptr) return;
    if (pane_host()->is_shutting_down()) return;
    auto *pane_state = bound_state_;
    if (pane_state == nullptr) return;
    capture_location();
    pane_state = bound_state_;
    if (pane_host()->is_shutting_down() || pane_state == nullptr) return;
    const bool closed_active = pane_state->active_tab_id == tab_id;
    if (!panedock::core::close_tab(
            *pane_state, tab_id,
            {L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", {}, {}}))
        return;
    if (closed_active && realized()) {
        auto *tab = active_tab();
        if (tab == nullptr) return;
        {
            ShellCall shell_call(pane_host());
            (void)navigate_to(tab->location);
        }
        if (pane_host()->is_shutting_down()) return;
    }
    pane_host()->tab_strip_needs_refresh(*this);
    pane_host()->schedule_session_save();
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
