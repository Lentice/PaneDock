#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "app_shell/pane_chrome_geometry.h"
#include "app_shell/pane_control_id.h"
#include "app_shell/pane_tab_strip.h"
#include "core/model.h"
#include "core/navigation.h"
#include "explorer_host/explorer_host.h"
#include "shell_core/shell_core.h"

namespace panedock::app_shell {

class PaneHost;

// The pane card metrics and the whole pane-chrome rect computation live in
// pane_chrome_geometry.h, which is pure and unit-tested.

inline RECT to_win32_rect(const TabStripRect &rect) noexcept {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

// A stable identity slot owning its Shell view, child windows and tab strip.
class Pane final {
  public:
    Pane() noexcept : tab_strip_ui_(this) {}
    ~Pane() { destroy(); }

    Pane(const Pane &) = delete;
    Pane &operator=(const Pane &) = delete;

    static bool register_window_class(HINSTANCE instance) noexcept;
    void set_host(PaneHost *host) noexcept { host_ = host; }
    PaneHost *pane_host() const noexcept { return host_; }
    bool create(HWND parent, int pane_index) noexcept;
    void destroy() noexcept;
    void window_destroyed(HWND window) noexcept;
    // The PaneState this slot currently displays. Not owned: it lives in
    // core::ApplicationState and outlives every rebind (PD-184 guarantees its
    // address is stable for the group's lifetime). Null when this slot is not
    // showing anything (no active Group, or a layout with fewer panes).
    void bind(panedock::core::PaneState *state) noexcept {
        bound_state_ = state;
    }
    void unbind() noexcept { bound_state_ = nullptr; }
    panedock::core::PaneState *pane_state() const noexcept {
        return bound_state_;
    }
    std::size_t index() const noexcept { return index_; }

    // The bound PaneState's active tab, or null when nothing is bound.
    //
    // NEVER store the returned pointer in a member or across a call that can
    // add or remove tabs: PD-184 guarantees the address of a PaneState, but
    // explicitly NOT the address of a TabState — PaneState::tabs is a vector
    // that push_back/insert/erase reallocates. Use it and drop it.
    panedock::core::TabState *active_tab() const noexcept;

    // PD-170 navigation identity for this pane: the generation plus the
    // group/tab it was issued for, so a result arriving after a Group or tab
    // switch can be discarded. Pane owns the request and comparison logic.
    panedock::core::NavigationRequest &pending_navigation() noexcept {
        return pending_navigation_;
    }

    using NavigationGeneration =
        panedock::explorer_host::ExplorerHost::NavigationGeneration;
    NavigationGeneration begin_navigation();
    HRESULT navigate_to(const panedock::core::ShellLocation &location);
    HRESULT navigate_up_one_level();
    bool navigation_request_is_current(
        NavigationGeneration generation);
    void navigation_complete(
        NavigationGeneration generation,
        const panedock::core::ShellLocation &location);
    void navigation_failed(NavigationGeneration generation);
    void navigate_history(bool back);
    void navigate_up();
    void refresh_view();

    void capture_view_mode();
    void capture_sort();
    void apply_view_mode();
    void apply_sort();
    void capture_location();
    void set_view_mode(const panedock::shell_core::ViewModeOption &option);
    void show_view_mode_menu(POINT screen);
    void submit_address();
    void pin_current_folder();
    void show_pinned_locations_menu(POINT screen);
    void switch_active_tab(const std::string &tab_id);
    void cycle_active_tab(bool reverse);
    void add_tab(panedock::core::ShellLocation initial_location);
    void close_tab(const std::string &tab_id);
    void close_tabs(const std::string &tab_id, int command);
    int show_tab_context_menu(const std::string &tab_id, POINT screen);

    // Enable/disable back, forward, up and folder-context from the bound
    // tab's history. Needs no coordinator state, so it lives here.
    void refresh_navigation_buttons() noexcept;
    void refresh_navigation_chrome();
    void refresh_status_bar() noexcept;
    bool handle_command(int id);
    bool draw_control(const DRAWITEMSTRUCT& item);
    std::optional<LRESULT> color_address_bar(HWND control, HDC dc) noexcept;
    // One shared address brush, released when the main window ends.
    static void release_address_bar_background_brush() noexcept;
    // Shared icon font: the coordinator releases it on DPI change/shutdown.
    static void release_navigation_icon_font() noexcept;

    void apply_container_region(int width, int height, int radius) noexcept;
    bool set_rect(const RECT &rect) noexcept;
    void set_paint_geometry(const RECT &navigation_background,
                            const RECT &pane_window_rect, UINT dpi) noexcept;
    void paint_background(HDC target) noexcept;
    void set_visible(bool visible) noexcept;
    void apply_font(HFONT font) noexcept;

    HWND window() const noexcept { return window_; }
    HWND explorer_container() const noexcept { return explorer_container_; }
    HWND tab_strip() const noexcept { return tab_strip_ui_.window(); }
    PaneTabStrip &tab_strip_ui() noexcept { return tab_strip_ui_; }
    const PaneTabStrip &tab_strip_ui() const noexcept { return tab_strip_ui_; }
    HWND address_bar() const noexcept { return address_bar_; }
    HWND status_bar() const noexcept { return status_bar_; }
    HWND back_button() const noexcept { return back_button_; }
    HWND forward_button() const noexcept { return forward_button_; }
    HWND up_button() const noexcept { return up_button_; }
    HWND refresh_button() const noexcept { return refresh_button_; }
    HWND view_mode_button() const noexcept { return view_mode_button_; }
    HWND pinned_button() const noexcept { return pinned_button_; }

    HWND folder_context_button() const noexcept {
        return folder_context_button_;
    }

    std::optional<RECT> &laid_out_pane_rect() noexcept {
        return laid_out_pane_rect_;
    }
    const std::optional<RECT> &laid_out_pane_rect() const noexcept {
        return laid_out_pane_rect_;
    }
    // --- Shell view lifetime (PD-183) --------------------------------
    //
    // Pane holds the ExplorerHost and its parent container HWND, so
    // realize()/derealize() can only be called through Pane's own
    // destroy() ordering (see pane.cpp) — the coordinator decides *when*
    // to realize/derealize via core::plan_realization, Pane just carries
    // that decision out. Pane never calls plan_realization itself.

    // Wraps ExplorerHost::initialize() against this pane's own
    // explorer_container(). Returns the initialize() HRESULT (not `bool`
    // per the ticket's literal signature) because callers need the exact
    // HRESULT to report first-failure diagnostics; see PD-183 交接區.
    HRESULT realize(const RECT &local_rect,
                    const panedock::core::ShellLocation &location) noexcept;
    void derealize() noexcept;
    bool realized() const noexcept { return realized_; }
    void set_suppress_history(bool suppress) noexcept {
        suppress_history_record_ = suppress;
    }
    bool suppress_history() const noexcept { return suppress_history_record_; }

    // Transitional direct access for ExplorerHost members that PD-183
    // does not promote to a Pane-level command/query (navigation
    // notification setup, view mode, sort, item counts, focus, ...). See
    // PD-183 交接區 for the call-site list still using this.
    panedock::explorer_host::ExplorerHost &host() noexcept {
        return explorer_host_;
    }
    const panedock::explorer_host::ExplorerHost &host() const noexcept {
        return explorer_host_;
    }

  private:
    // Called after a successful tab mutation. Closing an inactive tab only
    // refreshes chrome and saves; replacing the last tab still navigates.
    void finish_tab_change(bool navigate_active);

    PaneHost *host_{nullptr};
    panedock::core::PaneState *bound_state_{nullptr};
    std::size_t index_{};
    panedock::core::NavigationRequest pending_navigation_{};
    HWND window_{nullptr};
    HWND explorer_container_{nullptr};
    PaneTabStrip tab_strip_ui_;
    HWND address_bar_{nullptr};
    HWND status_bar_{nullptr};
    HWND back_button_{nullptr};
    HWND forward_button_{nullptr};
    HWND up_button_{nullptr};
    HWND refresh_button_{nullptr};
    HWND view_mode_button_{nullptr};
    HWND pinned_button_{nullptr};
    HWND folder_context_button_{nullptr};
    std::optional<RECT> laid_out_pane_rect_;
    panedock::explorer_host::ExplorerHost explorer_host_;
    bool realized_{};
    bool suppress_history_record_{};
    RECT navigation_background_{};
    UINT paint_dpi_{96};
    HDC paint_dc_{nullptr};
    HBITMAP paint_bitmap_{nullptr};
    HBITMAP paint_old_bitmap_{nullptr};
    SIZE paint_size_{};
    HPEN card_border_pen_{nullptr};
    UINT card_border_pen_dpi_{};
};

inline constexpr int kCloseTabId = 780;
inline constexpr int kCloseOtherTabsId = 781;
inline constexpr int kCloseAllTabsId = 782;
inline constexpr int kCloseTabsToRightId = 783;

inline constexpr int kViewModeMenuIdBase = 360;

inline constexpr int kPinnedMenuIdBase = 500;
inline constexpr int kPinnedMenuMaxLocationCount = 64;
inline constexpr int kPinnedMenuDesktopOffset = 0;
inline constexpr int kPinnedMenuThisPcOffset = 1;
inline constexpr int kPinnedMenuLocationOffset = 2;
inline constexpr int kPinnedMenuAddOffset =
    kPinnedMenuLocationOffset + kPinnedMenuMaxLocationCount;
inline constexpr int kPinnedMenuManageOffset = kPinnedMenuAddOffset + 1;
inline constexpr int kPinnedMenuSlotsPerPane = kPinnedMenuManageOffset + 1;
inline constexpr std::size_t kPinnedMenuFixedLocationCount = 2;

} // namespace panedock::app_shell
