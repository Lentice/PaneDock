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
#include <list>
#include <utility>
#include <span>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "file_operations/file_operations.h"
#include "managed_list/list_worker.h"
#include "managed_list/icon_source.h"
#include "managed_list/row_store.h"
#include "app_shell/pane_chrome_geometry.h"
#include "app_shell/pane_control_id.h"
#include "app_shell/pane_tab_strip.h"
#include "app_shell/session_writer.h"
#include "app_shell/shell_reentry_guard.h"
#include "core/model.h"
#include "core/bounded_order.h"
#include "core/bounded_lru.h"
#include "core/key_routing.h"
#include "core/navigation.h"
#include "explorer_host/context_menu.h"
#include "shell_core/shell_core.h"

namespace panedock::app_shell {

// Superclass of SysListView32 (see PD-250): unknown class name to MSAA/UIA proxies.
inline constexpr wchar_t kManagedListClassName[] = L"PaneDockManagedList";
inline constexpr UINT kRestoreListFocusMessage = WM_APP + 230;
// Posted by the list when the user resized or dragged a column: save the new layout.
inline constexpr UINT kColumnsChangedMessage = WM_APP + 231;
// Posted to the list on selection change: refresh the status bar once per burst.
inline constexpr UINT kSelectionStatusMessage = WM_APP + 232;
// Posted to the list when the rename edit lost focus: commit outside WM_KILLFOCUS.
inline constexpr UINT kCommitListRenameMessage = WM_APP + 233;
// Posted to the list when another program vetoed ejecting the displayed
// volume: re-subscribe through Refresh, outside the WM_DEVICECHANGE send.
inline constexpr UINT kListDeviceRefreshMessage = WM_APP + 234;
// Posted to the pane when a typed address failed to load: put the previous
// folder back and report, outside the batch that carried the failure.
inline constexpr UINT kAddressFailedMessage = WM_APP + 240;
inline constexpr UINT_PTR kListFocusRestoreTimerId = 0x5044;

class PaneHost;
class WindowPositionBatch;
class PaneListDropTarget;

// The pane card metrics and the whole pane-chrome rect computation live in
// pane_chrome_geometry.h, which is pure and unit-tested.

inline RECT to_win32_rect(const TabStripRect &rect) noexcept {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

// A stable identity slot owning its managed list, child windows and tab strip.
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
        if (bound_state_ != state) {
            save_list_cache(true);
            cancel_managed_list(false);
        }
        bound_state_ = state;
    }
    // Hiding a pane (fewer panes in the new layout or Group) keeps its PD-261
    // snapshots, including the outgoing completed list; only destroy() and
    // discard_group_list_cache() remove owner entries.
    void unbind() noexcept {
        save_list_cache(true);
        cancel_managed_list(false);
        bound_state_ = nullptr;
    }
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

    using NavigationGeneration = std::uint64_t;
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
    // After PaneDock's own file operation: a list with no live change watch
    // (virtual location, failed or refused watch) reconciles once.
    void refresh_unwatched_list() noexcept;
    // A volume arrived or was removed (WM_DEVICECHANGE unit mask). This PC
    // refreshes; a pane on a removed drive moves to This PC, like Explorer.
    void volume_changed(DWORD units, bool removed) noexcept;
    // DBT_DEVTYP_HANDLE notifications registered on the list window.
    LRESULT list_device_change(WPARAM event, LPARAM data) noexcept;

    // Optional controlled Source; set before realize(). The source
    // seam accepts controlled owned batches in behavioral tests, never a Shell host.
    void use_managed_list(std::unique_ptr<managed_list::Source> source = {});
    struct ListSnapshot final {
        managed_list::Request request;
        std::vector<managed_list::Row> rows;
        managed_list::Phase phase{managed_list::Phase::idle};
        DWORD error{};
        managed_list::WatchState watch{managed_list::WatchState::inactive};
        DWORD watch_error{};
        bool updating{};
        // Keep Request untouched for batch/enrichment identity checks.
        std::optional<core::ShellLocation> resolved_location{};
        std::optional<core::ShellLocation> parent_location{};
        managed_list::ParentOutcome parent_outcome{managed_list::ParentOutcome::unresolved};
        managed_list::NotificationCapability notification{managed_list::NotificationCapability::filesystem_watch};
    };
    using ListCacheStats = core::LruStats;
    static ListCacheStats list_cache_stats() noexcept;
    static void limit_list_cache_bytes(std::size_t bytes) noexcept;
    // A deleted Group's id can be issued again by next_group_id.
    static void discard_group_list_cache(const std::string& group_id) noexcept;
    // Borrowed until the next Pane operation; never keep across a Shell pump.
    const ListSnapshot& list_snapshot() const noexcept { return list_; }
    struct ListItemIdentity final {
        // Borrowed parent location and stable key (the legacy name member).
        // Display text is available only through Row::name.
        std::wstring_view location;
        std::wstring_view name;
        bool shell_item{};
        bool operator==(const ListItemIdentity&) const noexcept = default;
    };
    struct ListInteractionSnapshot final {
        std::vector<ListItemIdentity> selected;
        std::optional<ListItemIdentity> focused;
        std::optional<ListItemIdentity> anchor;
        std::optional<ListItemIdentity> viewport_anchor;
        int viewport_offset{};
        bool viewport_anchor_clamped{};
        bool operator==(const ListInteractionSnapshot&) const = default;
    };
    // Selection, keyboard focus, selection mark, and the top visible row are
    // reported as identities, never row numbers. The viewport clamp flag
    // reports when the most recent restore hit the list's end bound. Returned
    // views expire on the next Pane operation.
    ListInteractionSnapshot list_interaction_snapshot() const;
    // Owned-value enrichment seam. Viewport indexes are immediately converted
    // to identities; they are never used to address a completion.
    void use_list_icons(std::unique_ptr<managed_list::IconSource> source);
    void set_list_icon_viewport(std::size_t first, std::size_t count) noexcept;
    int list_item_image(std::size_t row) const noexcept;
    std::size_t list_image_bytes() const noexcept {
        if (!icon_images_) return 0;
        auto bytes = (icon_cache_.size() + 2) * managed_list::image_bytes(icon_size_);
        for (const auto& entry : icon_cache_)
            bytes += sizeof(CachedIcon) + managed_list::owned_icon_key_bytes(entry.key);
        return bytes;
    }
    using RecoveryWork = managed_list::RowStore::RecoveryWork;
    // Deterministic recovery work evidence for the bounded list harness.
    RecoveryWork list_recovery_work() const noexcept { return row_store_.work(); }
    std::size_t list_index_bytes() const noexcept { return row_store_.index_bytes(); }
    std::size_t list_index_peak_bytes() const noexcept { return row_store_.index_peak_bytes(); }
    // Harness can lower, never raise, the production ceiling before realize.
    void limit_list_index_bytes(std::size_t bytes) noexcept {
        if (!realized_) row_store_.limit_index_bytes(bytes);
    }
    bool list_property_sort_pending() const noexcept { return property_sort_pending_; }
    bool list_property_capacity_failed() const noexcept { return property_capacity_failed_; }
    HANDLE icon_event() const noexcept;
    void process_icon_results() noexcept;
    HANDLE content_event() const noexcept;
    void process_content_results() noexcept;
    bool open_list_row(std::size_t row);
    // Enter: one row opens like a double-click; several selected rows get
    // their default verb together through one Shell context menu.
    bool open_list_selection();
    std::optional<LRESULT> list_notification(NMHDR* notification) noexcept;
    // Details columns (PD-257): shown set, order and widths live in the tab.
    void apply_list_columns() noexcept;
    void capture_list_columns() noexcept;
    void toggle_list_column(std::string_view id) noexcept;
    void show_list_column_menu(POINT screen) noexcept;
    const std::vector<std::string>& list_column_ids() const noexcept { return column_ids_; }
    // Header position <-> stable subitem (core::kKnownColumns index); -1 when unknown.
    int list_column_subitem(int header_index) const noexcept;
    int list_column_index(std::string_view id) const noexcept;

    // Content operations stay behind Pane; callers never borrow the concrete
    // Shell host or its location across a message-pumping call.
    struct SelectionSnapshot final {
        int total{};
        int selected{};
        unsigned long long selected_bytes{};
        bool selected_bytes_valid{};
    };
    struct FileCommandSnapshot final {
        core::NavigationRequest binding;
        core::ShellLocation folder;
        std::vector<std::wstring> items; // owned absolute parsing names
    };
    HRESULT file_command_snapshot(file_operations::Command command,
                                  FileCommandSnapshot& snapshot) const noexcept;
    bool file_command_is_current(const FileCommandSnapshot& snapshot) const noexcept;
    // Exactly one selected identity. Enter and focus loss commit through Shell
    // (Explorer parity; overrides PD-237's cancel on focus loss); Escape,
    // accepted batches, sort/geometry, navigation and shutdown cancel.
    bool begin_list_rename() noexcept;
    // Whether a key message was sent to this active pane's managed list, the
    // core::KeyInput::list_focused that core::resolve_key routes list keys by.
    bool list_has_key(const MSG& message) const noexcept;
    // Performs a routed list key this pane owns (F2, Ctrl+A, Ctrl+Shift+N,
    // Enter) and consumes it. False for any other action, including the file
    // commands, which run through the host's file-operation bracket.
    bool perform_list_key(core::KeyAction action) noexcept;
    void select_all_list_rows() noexcept;
    // Creates "New folder" in the displayed folder through the host, then
    // selects it and begins rename once its row arrives, as Explorer does.
    bool create_new_folder() noexcept;
    void commit_list_rename_after_focus_loss() noexcept;
    void show_status_text(const wchar_t* text) noexcept;
    void cancel_list_rename() noexcept;
    void restore_posted_list_focus() noexcept;
    void note_list_focus_activation() noexcept;
    void restore_list_focus_after_activation() noexcept;
    bool list_edit_has_focus() const noexcept;
    HRESULT list_rename_result() const noexcept { return list_rename_result_; }
    // OLE entry point also used by the non-transfer harness.
    Microsoft::WRL::ComPtr<IDropTarget> list_drop_target() const noexcept { return list_drop_target_; }
    void use_list_drop_resolver(std::unique_ptr<file_operations::DropTargetResolver> resolver) {
        if (!realized_) list_drop_resolver_ = std::move(resolver);
    }
    // client_point is NMLISTVIEW::ptAction; a right-button drag lets the Shell
    // drop target show its Copy/Move/Create shortcut menu, as in Explorer.
    HRESULT begin_list_drag(POINT client_point, bool right_button = false) noexcept;
    static HRESULT list_drag_continue(bool right_button, bool escape, DWORD keys,
                                      bool cancelled) noexcept;
    // Ctrl+X from this list dims those rows (LVIS_CUT) until the clipboard
    // sequence changes. Bounded by the cut selection; no timer.
    void note_list_cut(const FileCommandSnapshot& snapshot) noexcept;
    void list_clipboard_changed() noexcept;
    // LVIS_CUT / LVIS_DROPHILITED for one row; supplied through the callback mask.
    UINT list_transfer_state(const managed_list::Row& row) const noexcept;
    HRESULT selection_snapshot(SelectionSnapshot &snapshot) noexcept;
    panedock::core::ShellLocation content_location() const;
    void focus_content() noexcept;
    // Ctrl+L / Alt+D / F4: focus the address bar with its text selected.
    void focus_address_bar() noexcept;
    // Escape in the address bar: show the current location again and
    // return focus to the list, as Explorer does.
    void cancel_address_edit() noexcept;
    struct ContextMenuSnapshot final {
        FileCommandSnapshot target;
        explorer_host::ContextMenuRequest request;
    };
    HRESULT context_menu_snapshot(POINT screen, bool background,
                                  ContextMenuSnapshot& snapshot) noexcept;
    bool show_content_context_menu(HWND target, POINT screen) noexcept;
    bool show_folder_context_menu(HWND owner, POINT screen) noexcept;
    void use_context_menu_presenter(std::unique_ptr<explorer_host::ContextMenuPresenter> presenter) {
        if (!realized_) context_menu_presenter_ = std::move(presenter);
    }

    void apply_view_mode();
    void apply_sort();
    void set_view_mode(const panedock::shell_core::ViewModeOption &option);
    void show_view_mode_menu(POINT screen);
    void submit_address();
    // kAddressFailedMessage: restore the previous folder after a typed
    // address failed, open it if it was a file, otherwise report the error.
    void finish_failed_address() noexcept;
    void pin_current_folder();
    void show_pinned_locations_menu(POINT screen);
    // One pinned-menu slot (kPinnedMenu*Offset): navigate, add or manage.
    void run_pinned_menu_slot(int slot);
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
    void set_shell_rect(const RECT& rect, WindowPositionBatch* deferred) noexcept;
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
    // Pane owns list realization and child HWND lifetime. The coordinator
    // chooses visibility via core::plan_realization; Pane applies that plan.
    HRESULT realize(const RECT &local_rect,
                    const panedock::core::ShellLocation &location) noexcept;
    void derealize() noexcept;
    bool realized() const noexcept { return realized_; }
    void set_suppress_history(bool suppress) noexcept {
        suppress_history_record_ = suppress;
    }
    bool suppress_history() const noexcept { return suppress_history_record_; }

  private:
    bool present_context_menu(HWND owner, ContextMenuSnapshot snapshot) noexcept;
    // Opens one non-directory row as Explorer does: a shortcut to a folder,
    // or a file that is a Shell folder (.zip), navigates this pane instead
    // of opening another Explorer window; anything else gets its default verb.
    bool invoke_default_list_row_verb(std::size_t row) noexcept;
    // One Shell context menu over several rows: their default verb together.
    bool invoke_default_list_rows_verb(std::span<const std::size_t> rows) noexcept;
    int show_message(const wchar_t* text, UINT type) noexcept;
    struct TypedAddress final {
        NavigationGeneration generation{};
        std::wstring text;
    };
    // The user-typed navigation still waiting for its first batch, and the
    // one that failed and waits for kAddressFailedMessage.
    std::optional<TypedAddress> typed_address_;
    std::optional<TypedAddress> failed_address_;
    std::unique_ptr<explorer_host::ContextMenuPresenter> context_menu_presenter_;
    // The injected presenter, else the Shell's own.
    explorer_host::ContextMenuPresenter& menu_presenter() const noexcept {
        return context_menu_presenter_ ? *context_menu_presenter_
                                       : explorer_host::native_context_menu_presenter();
    }
    bool context_menu_busy_{};
    friend class PaneListDropTarget;
    HRESULT register_list_drop_target() noexcept;
    void revoke_list_drop_target() noexcept;
    Microsoft::WRL::ComPtr<IDropTarget> list_drop_target_;
    std::unique_ptr<file_operations::DropTargetResolver> list_drop_resolver_;
    bool list_source_drag_active_{};
    // Identity-based so re-sorting or batches never move the mark to another row.
    void set_list_drop_highlight(std::optional<managed_list::ItemIdentity> item,
                                 int index_hint) noexcept;
    void clear_list_cut() noexcept;
    std::optional<managed_list::ItemIdentity> list_drop_highlight_;
    int list_drop_highlight_index_{-1};
    struct ListCut final {
        core::ShellLocation folder;
        std::vector<managed_list::ItemIdentity> items; // sorted
        DWORD sequence{};
    } list_cut_;
    struct ManagedListInteraction final {
        std::vector<managed_list::ItemIdentity> selected_items;
        std::optional<managed_list::ItemIdentity> focused_item;
        std::optional<managed_list::ItemIdentity> anchor_item;
        std::optional<managed_list::ItemIdentity> viewport_anchor_item;
        int viewport_offset{};
        int focused_index{-1};
        int viewport_index{-1};
    };
    struct CachedList final {
        const Pane* owner{};
        ListSnapshot snapshot;
        ManagedListInteraction interaction;
    };
    // 4 visible lists * 24 MiB + 32 MiB inactive <= 128 MiB row target.
    static constexpr std::size_t kListCacheBytes = 32 * 1024 * 1024;
    // STA-only process owner: independent of pane coordinator and images.
    static core::BoundedLru<CachedList>& list_cache() noexcept;
    void prune_list_cache(bool all = false) noexcept;
    // True when the current list was admitted (and so moved out of list_).
    bool save_list_cache(bool leaving_group = false) noexcept;
    std::optional<CachedList> take_list_cache(const managed_list::Request& request);
    void cancel_managed_list(bool discard_cache = true) noexcept;
    void clear_managed_list_interaction() noexcept;
    ManagedListInteraction capture_managed_list_interaction() const;
    void restore_managed_list_interaction(
        const ManagedListInteraction& interaction) noexcept;
    void apply_managed_list_sort() noexcept;
    // sorted_prefix is the prior, already ordered run for a pure enumeration append.
    // Zero keeps the full-sort behavior for sort changes and row mutations.
    void sort_managed_list_rows(std::size_t sorted_prefix = 0) noexcept;
    void update_managed_list_sort_indicator() noexcept;
    void set_managed_list_sort(panedock::shell_core::SortProperty property);
    HRESULT start_managed_list(const core::ShellLocation& location,
                               NavigationGeneration generation);
    void update_managed_list() noexcept;
    void queue_selection_status() noexcept;
    void apply_list_changes(managed_list::Batch& batch, ManagedListInteraction& interaction);
    // Drops cached images of these identities only; other rows keep theirs.
    void forget_list_icons(std::vector<managed_list::ItemIdentity> identities) noexcept;
    // An F2 edit survives unrelated batches: it follows its row, and is
    // canceled only when that row is gone or scrolled out of view.
    void keep_list_rename_in_place() noexcept;
    // Eject support: the worker's zero-access volume handle, registered for
    // DBT_DEVICEQUERYREMOVE. Released on navigation, watch end and eject.
    void register_list_device(std::shared_ptr<void> handle) noexcept;
    void release_list_device() noexcept;
    std::shared_ptr<void> list_device_handle_;
    HDEVNOTIFY list_device_notify_{};
    bool list_navigation_pending_{};
    // History cursor stays at the last success until this target is accepted.
    std::optional<std::size_t> list_history_target_;
    std::optional<std::size_t> list_retry_history_target_;
    HRESULT navigate_content(core::ShellLocation location,
                             NavigationGeneration generation);
    void resolve_list_navigation(bool success);
    std::unique_ptr<managed_list::Source> list_source_;
    ListSnapshot list_;
    // The rows' byte budget, recovery index and change merge; list_.rows is
    // the vector it works on.
    managed_list::RowStore row_store_;
    void fail_list_watch(DWORD error) noexcept;
    struct ListRename final {
        FileCommandSnapshot snapshot;
        managed_list::ItemIdentity identity;
        // Extension Explorer hides for this local file (e.g. L".txt"); the edit
        // shows the display name and the commit restores it unless typed.
        std::wstring hidden_extension;
    };
    std::optional<ListRename> list_rename_;
    // Edit text captured at WM_KILLFOCUS; committed by kCommitListRenameMessage
    // unless the edit regains focus or the rename is canceled first.
    std::optional<std::wstring> list_rename_focus_commit_;
    struct PendingNewItem final {
        core::NavigationRequest binding;
        std::wstring parsing_name, leaf_name;
    };
    // At most one; dropped on navigation or once its row has been found.
    std::optional<PendingNewItem> pending_new_item_;
    void rename_pending_new_item() noexcept;
    std::optional<core::NavigationRequest> pending_list_focus_restore_;
    bool list_focus_restore_posted_{};
    bool list_focus_restore_timer_used_{};
    bool list_focus_restore_activation_seen_{};
    HWND list_rename_edit_{};
    HFONT list_rename_font_{};
    std::vector<std::string> column_ids_;  // header position -> column id
    std::string applied_columns_;          // TabState::columns the header currently shows
    UINT applied_columns_dpi_{};           // DPI the header widths were scaled for
    bool selection_status_posted_{};       // one coalesced status update per selection burst
    bool list_rename_busy_{};
    HRESULT list_rename_result_{S_FALSE};
    void commit_list_rename(std::wstring name) noexcept;
    static LRESULT CALLBACK list_edit_subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    HWND list_window_{};
    std::unique_ptr<managed_list::IconSource> icon_source_;
    struct CachedIcon final {
        managed_list::IconKey key;
        int slot{};
        std::uint64_t used{};
    };
    std::vector<CachedIcon> icon_cache_;
    std::vector<managed_list::IconKey> icon_interest_;
    HIMAGELIST icon_images_{};
    int icon_size_{16};
    int icon_cell_height_{16}; // image-list cell height; sets the Details row height
    UINT icon_dpi_{96};
    bool icon_thumbnail_{};
    FOLDERVIEWMODE list_mode_{FVM_DETAILS};
    shell_core::ViewModeSelection list_view_selection_{FVM_DETAILS, -1};
    // A geometry transaction retains one viewport item, never a row snapshot.
    class ListGeometryScope final {
    public:
        explicit ListGeometryScope(Pane& pane) noexcept;
        ~ListGeometryScope();
    private:
        Pane& pane_;
        HWND window_{};
        NavigationGeneration generation_{};
        int index_{-1};
        std::optional<managed_list::ItemIdentity> identity_;
        POINT position_{};
        DWORD view_{};
    };
    std::optional<ListGeometryScope> layout_list_geometry_;
    void apply_managed_list_view_mode() noexcept;
    // "This PC" always shows drive cards; the tab's own view mode is untouched.
    bool this_pc_cards() const noexcept;
    shell_core::ViewModeSelection effective_list_view_selection(const core::TabState* tab) const noexcept;
    void request_list_images(std::vector<std::size_t> rows, UINT dpi) noexcept;
    std::optional<LRESULT> draw_list_card(NMHDR*) noexcept;
    std::uint64_t icon_viewport_{}, icon_clock_{};
    bool icon_viewport_posted_{};
    bool icon_seam_{};
    std::array<bool, managed_list::kIconInterest> image_attempted_{};
    std::vector<managed_list::IconKey> type_attempted_; // stage-one requests tried this viewport
    // Every distinct file type of the list (capped), so type icons are loaded ahead of
    // scrolling and stay cached; found by scanning rows incrementally (PD-258).
    std::vector<managed_list::IconKey> type_pool_;
    std::size_t type_pool_cursor_{};
    managed_list::Request type_pool_request_;
    std::vector<managed_list::IconRequest> enrichment_interest_;
    std::size_t property_sort_cursor_{};
    bool property_sort_pending_{};
    bool property_capacity_failed_{};
    std::string property_sort_key_;
    core::BoundedOrder property_order_;
    bool property_row_less(std::size_t left, std::size_t right) const noexcept;
    int expensive_sort_slot() const noexcept;
    void append_property_requests(std::vector<managed_list::IconRequest>& requests,
                                  const std::vector<std::size_t>& visible);
    void accept_property_result(managed_list::IconResult& result);
    void finish_property_batch() noexcept;
    managed_list::IconKey list_icon_key(std::size_t row) const;
    std::optional<managed_list::IconKey> list_type_icon_key(std::size_t row) const;
    void append_type_icon_requests(std::vector<managed_list::IconRequest>& requests,
                                   const std::vector<std::size_t>& visible);
    void refresh_list_icon_viewport() noexcept;
    void queue_list_icon_viewport() noexcept;
    void cancel_list_icons() noexcept;
    void destroy_list_icons() noexcept;
    bool ensure_list_icon_images(int size) noexcept;
    static LRESULT CALLBACK list_icon_subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    bool last_viewport_anchor_clamped_{};
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
    NavigationGeneration navigation_generation_{};
    bool realized_{};
    bool suppress_history_record_{};
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
