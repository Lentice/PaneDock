#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "managed_list/list_worker.h"
#include "app_shell/pane_chrome_geometry.h"
#include "app_shell/pane_control_id.h"
#include "app_shell/pane_tab_strip.h"
#include "app_shell/session_writer.h"
#include "app_shell/shell_reentry_guard.h"
#include "core/model.h"
#include "core/navigation.h"
#include "explorer_host/explorer_host.h"
#include "shell_core/shell_core.h"

namespace panedock::app_shell {

class PaneHost;
class WindowPositionBatch;

// The pane card metrics and the whole pane-chrome rect computation live in
// pane_chrome_geometry.h, which is pure and unit-tested.

inline RECT to_win32_rect(const TabStripRect &rect) noexcept {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

// A stable identity slot owning its Shell view, child windows and tab strip.
class Pane final {
  public:
    struct LayoutResult final {
        bool changed{};
        RECT shell_rect{};
        RECT container_rect{};
    };
    Pane() noexcept : tab_strip_ui_(this) {}
    ~Pane() { destroy(); }

    Pane(const Pane &) = delete;
    Pane &operator=(const Pane &) = delete;

    static bool register_window_class(HINSTANCE instance) noexcept;
    void set_host(PaneHost *host) noexcept { host_ = host; }
    PaneHost *pane_host() const noexcept { return host_; }
    // Set with set_host, before create(); every Shell call the pane makes
    // holds a ShellCallScope on it.
    void set_reentry_guard(ShellReentryGuard *guard) noexcept {
        reentry_guard_ = guard;
    }
    ShellReentryGuard &reentry_guard() const noexcept {
        return *reentry_guard_;
    }
    // Set with set_host: whether a Group transition forbids reading the live
    // view's location into the model right now (PD-206).
    void set_capture_gate(const LocationCaptureGate *gate) noexcept {
        capture_gate_ = gate;
    }
    // The one guard every Shell-touching Pane method needs: a host exists and
    // is not tearing down. Shell calls re-enter our message loop, so methods
    // re-check this after every ShellCallScope, not just on entry.
    bool active() const noexcept;
    bool create(HWND parent, int pane_index) noexcept;
    void destroy() noexcept;
    void window_destroyed(HWND window) noexcept;
    // The PaneState this slot currently displays. Not owned: it lives in
    // core::ApplicationState and outlives every rebind (PD-184 guarantees its
    // address is stable for the group's lifetime). Null when this slot is not
    // showing anything (no active Group, or a layout with fewer panes).
    void bind(panedock::core::PaneState *state) noexcept {
        if (bound_state_ != state) cancel_managed_list();
        bound_state_ = state;
    }
    void unbind() noexcept { cancel_managed_list(); bound_state_ = nullptr; }
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

    // Process-local development content choice; set before realize(). The source
    // seam accepts controlled owned batches in behavioral tests, never a Shell host.
    void use_managed_list(std::unique_ptr<managed_list::Source> source = {});
    struct ListSnapshot final {
        managed_list::Request request;
        std::vector<managed_list::Row> rows;
        managed_list::Phase phase{managed_list::Phase::idle};
        DWORD error{};
    };
    // Borrowed until the next Pane operation; never keep across a Shell pump.
    const ListSnapshot& list_snapshot() const noexcept { return list_; }
    struct ListItemIdentity final {
        // A local item is identified by its containing Shell location and leaf
        // name. These views are borrowed from list_snapshot().
        std::wstring_view location;
        std::wstring_view name;
        bool operator==(const ListItemIdentity&) const noexcept = default;
    };
    struct ListInteractionSnapshot final {
        std::vector<ListItemIdentity> selected;
        std::optional<ListItemIdentity> focused;
        std::optional<ListItemIdentity> anchor;
        bool operator==(const ListInteractionSnapshot&) const = default;
    };
    // Selection, keyboard focus and the range-selection mark are reported as
    // item identities, never as row numbers. Returned views expire on the
    // next Pane operation.
    ListInteractionSnapshot list_interaction_snapshot() const;
    HANDLE content_event() const noexcept;
    void process_content_results() noexcept;
    bool open_list_row(std::size_t row);
    std::optional<LRESULT> list_notification(NMHDR* notification) noexcept;

    // Content operations stay behind Pane; callers never borrow the concrete
    // Shell host or its location across a message-pumping call.
    struct SelectionSnapshot final {
        int total{};
        int selected{};
        unsigned long long selected_bytes{};
        bool selected_bytes_valid{};
    };
    HRESULT selection_snapshot(SelectionSnapshot &snapshot) noexcept;
    panedock::core::ShellLocation content_location() const;
    void focus_content() noexcept;
    HRESULT translate_content_accelerator(MSG *message) noexcept;
    void process_content_retry() noexcept;
    bool show_folder_context_menu(HWND owner, POINT screen) noexcept;

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
    // Activating, adding and closing a tab by strip position is this pane's
    // own strip: the pane resolves the index or the point itself instead of
    // routing it through the coordinator.
    void activate_tab_at(std::size_t item);
    void add_default_tab();
    void close_tab_at_screen(POINT screen);
    void close_tab(const std::string &tab_id);
    void close_tabs(const std::string &tab_id, int command);
    // A right-click on this pane's tab strip is this pane's own business:
    // it hit-tests, shows the menu and closes the tabs itself.
    void handle_tab_context_menu(POINT screen);

    // Enable/disable back, forward, up and folder-context from the bound
    // tab's history. Needs no coordinator state, so it lives here.
    void refresh_navigation_buttons() noexcept;
    void refresh_navigation_chrome();
    void refresh_status_bar() noexcept;
    bool handle_command(int id);
    bool draw_control(const DRAWITEMSTRUCT& item);
    // PD-221: the coordinator decides which pane is active; the pane only
    // paints its chrome for it. Invalidates the chrome when the flag changes.
    void set_chrome_active(bool active) noexcept;
    bool chrome_active() const noexcept { return chrome_active_; }
    std::optional<LRESULT> color_address_bar(HWND control, HDC dc) noexcept;
    // One shared address brush, released when the main window ends.
    static void release_address_bar_background_brush() noexcept;
    // Shared icon font: the coordinator releases it on DPI change/shutdown.
    static void release_navigation_icon_font() noexcept;

    bool set_rect(const RECT &rect) noexcept;
    void paint_background(HDC target) noexcept;
    LayoutResult stage_layout(const RECT& pane_rect, UINT dpi,
                              WindowPositionBatch& outer,
                              WindowPositionBatch& children) noexcept;
    void finish_layout(const LayoutResult& layout, UINT dpi) noexcept;
    void set_shell_rect(const RECT& rect, HDWP* deferred) noexcept;
    void set_shell_visible(bool visible) noexcept;
    HRESULT configure_realized_view() noexcept;
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

  private:
    void cancel_managed_list() noexcept;
    void clear_managed_list_interaction() noexcept;
    HRESULT start_managed_list(const core::ShellLocation& location,
                               NavigationGeneration generation);
    void update_managed_list() noexcept;
    bool managed_list_enabled_{}; // process opt-in, never persisted
    bool managed_list_active_{};  // current content; namespace tabs use Shell
    bool list_navigation_pending_{};
    // History cursor stays at the last success until this target is accepted.
    std::optional<std::size_t> list_history_target_;
    std::optional<managed_list::Request> pending_list_route_;
    HRESULT navigate_content(core::ShellLocation location,
                             NavigationGeneration generation);
    void resolve_list_navigation(bool success);
    std::unique_ptr<managed_list::Source> list_source_;
    ListSnapshot list_;
    HWND list_window_{};
    std::size_t list_name_bytes_{};
    HRESULT realize_managed_list(const RECT& rect, const core::ShellLocation& location,
                                 NavigationGeneration generation);
    // Owned identity only: no borrowed TabState survives a Shell pump.
    panedock::core::NavigationRequest content_request() const;
    bool content_request_is_current(
        const panedock::core::NavigationRequest &request) const noexcept;
    void apply_container_region(int width, int height, int radius) noexcept;
    void set_paint_geometry(const PaneChromeRects& rects, UINT dpi) noexcept;
    std::array<HWND, 10> chrome_children() const noexcept;
    void repaint_chrome() noexcept;
    // Called after a successful tab mutation. Closing an inactive tab only
    // refreshes chrome and saves; replacing the last tab still navigates.
    void finish_tab_change(bool navigate_active);

    PaneHost *host_{nullptr};
    ShellReentryGuard *reentry_guard_{nullptr};
    const LocationCaptureGate *capture_gate_{nullptr};
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
    // The history slot navigate_history left, kept until that navigation is
    // resolved so navigation_failed can put the tab back. Carries the
    // generation it belongs to: a later navigation that fails must not roll
    // back a history move that already completed.
    std::optional<std::pair<NavigationGeneration, std::size_t>>
        history_rollback_;
    RECT navigation_background_{};
    // Pane-local bottom of the navigation row: the card band tinted while the
    // pane is active.
    int header_bottom_{};
    bool chrome_active_{};
    UINT paint_dpi_{96};
    HDC paint_dc_{nullptr};
    HBITMAP paint_bitmap_{nullptr};
    HBITMAP paint_old_bitmap_{nullptr};
    SIZE paint_size_{};
    HPEN card_border_pen_{nullptr};
    UINT card_border_pen_dpi_{};
};

} // namespace panedock::app_shell
