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
#include "app_shell/pane_chrome_geometry.h"
#include "app_shell/pane_control_id.h"
#include "app_shell/pane_tab_strip.h"
#include "app_shell/session_writer.h"
#include "app_shell/shell_reentry_guard.h"
#include "core/model.h"
#include "core/bounded_order.h"
#include "core/navigation.h"
#include "explorer_host/context_menu.h"
#include "shell_core/shell_core.h"

namespace panedock::app_shell {

// Superclass of SysListView32 (see PD-250): unknown class name to MSAA/UIA proxies.
inline constexpr wchar_t kManagedListClassName[] = L"PaneDockManagedList";
inline constexpr UINT kRestoreListFocusMessage = WM_APP + 230;
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
    struct ListCacheStats final {
        std::size_t bytes{}, entries{}, hits{}, evictions{}, limit{};
    };
    static ListCacheStats list_cache_stats() noexcept;
    static void limit_list_cache_bytes(std::size_t bytes) noexcept;
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
    struct RecoveryWork final {
        std::size_t indexed_rows{}, lookups{}, comparisons{}, accounted_rows{};
    };
    // Deterministic recovery work evidence for the bounded list harness.
    RecoveryWork list_recovery_work() const noexcept { return recovery_work_; }
    std::size_t list_index_bytes() const noexcept { return index_memory_.used; }
    std::size_t list_index_peak_bytes() const noexcept { return index_memory_.peak; }
    // Harness can lower, never raise, the production ceiling before realize.
    void limit_list_index_bytes(std::size_t bytes) noexcept {
        if (!realized_) index_memory_.limit = std::min(bytes, managed_list::kMaxIndexBytes);
    }
    bool list_property_sort_pending() const noexcept { return property_sort_pending_; }
    bool list_property_capacity_failed() const noexcept { return property_capacity_failed_; }
    HANDLE icon_event() const noexcept;
    void process_icon_results() noexcept;
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
    struct FileCommandSnapshot final {
        core::NavigationRequest binding;
        core::ShellLocation folder;
        std::vector<std::wstring> items; // owned absolute parsing names
    };
    std::optional<file_operations::Command> file_command_for_key(
        const MSG& message, bool control, bool alt, bool shift) const noexcept;
    HRESULT file_command_snapshot(file_operations::Command command,
                                  FileCommandSnapshot& snapshot) const noexcept;
    bool file_command_is_current(const FileCommandSnapshot& snapshot) const noexcept;
    // Exactly one selected identity. Enter commits through Shell; Escape,
    // focus loss, accepted batches, sort/geometry and navigation cancel.
    bool handle_list_rename_key(const MSG&, bool control, bool alt, bool shift) noexcept;
    bool begin_list_rename() noexcept;
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
    HRESULT begin_list_drag() noexcept;
    HRESULT selection_snapshot(SelectionSnapshot &snapshot) noexcept;
    panedock::core::ShellLocation content_location() const;
    void focus_content() noexcept;
    HRESULT translate_content_accelerator(MSG *message) noexcept;
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
    std::unique_ptr<explorer_host::ContextMenuPresenter> context_menu_presenter_;
    bool context_menu_busy_{};
    friend class PaneListDropTarget;
    HRESULT register_list_drop_target() noexcept;
    void revoke_list_drop_target() noexcept;
    Microsoft::WRL::ComPtr<IDropTarget> list_drop_target_;
    std::unique_ptr<file_operations::DropTargetResolver> list_drop_resolver_;
    bool list_source_drag_active_{};
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
        std::size_t bytes{};
    };
    struct ListCache final {
        // 4 visible lists * 24 MiB + 32 MiB inactive <= 128 MiB row target.
        // STA-only process owner: independent of pane coordinator and images.
        std::list<CachedList> entries;
        ListCacheStats stats{0, 0, 0, 0, 32 * 1024 * 1024};
    };
    static ListCache& list_cache() noexcept;
    void prune_list_cache(bool all = false) noexcept;
    void save_list_cache() noexcept;
    std::optional<CachedList> take_list_cache(const managed_list::Request& request);
    void cancel_managed_list() noexcept;
    void clear_managed_list_interaction() noexcept;
    ManagedListInteraction capture_managed_list_interaction() const;
    void restore_managed_list_interaction(
        const ManagedListInteraction& interaction) noexcept;
    void apply_managed_list_sort() noexcept;
    void sort_managed_list_rows() noexcept;
    void update_managed_list_sort_indicator() noexcept;
    void set_managed_list_sort(panedock::shell_core::SortProperty property);
    HRESULT start_managed_list(const core::ShellLocation& location,
                               NavigationGeneration generation);
    void update_managed_list() noexcept;
    void apply_list_changes(managed_list::Batch& batch, ManagedListInteraction& interaction);
    bool list_navigation_pending_{};
    // History cursor stays at the last success until this target is accepted.
    std::optional<std::size_t> list_history_target_;
    std::optional<std::size_t> list_retry_history_target_;
    HRESULT navigate_content(core::ShellLocation location,
                             NavigationGeneration generation);
    void resolve_list_navigation(bool success);
    std::unique_ptr<managed_list::Source> list_source_;
    ListSnapshot list_;
    mutable RecoveryWork recovery_work_;
    struct RowIdentityLess final {
        using is_transparent = void;
        const Pane* pane{};
        const managed_list::ItemIdentity& identity(std::size_t index) const { return pane->list_.rows[index].identity; }
        const managed_list::ItemIdentity& identity(const managed_list::ItemIdentity& value) const { return value; }
        template<class A, class B> bool operator()(const A& a, const B& b) const {
            ++pane->recovery_work_.comparisons;
            // Preserve PD-232's exact, case-sensitive leaf comparison. Display
            // sorting's case folding must never merge distinct local keys.
            return identity(a) < identity(b);
        }
    };
    struct IndexMemory final {
        std::size_t used{}, peak{}, limit{managed_list::kMaxIndexBytes};
    } index_memory_;
    template<class T> struct IndexAllocator final {
        using value_type = T;
        IndexMemory* memory{};
        explicit IndexAllocator(IndexMemory* value) noexcept : memory(value) {}
        template<class U> IndexAllocator(const IndexAllocator<U>& other) noexcept : memory(other.memory) {}
        T* allocate(std::size_t n) {
            if (memory->used > memory->limit || n > (memory->limit - memory->used) / sizeof(T))
                throw std::bad_alloc();
            auto* result = std::allocator<T>{}.allocate(n);
            memory->used += n * sizeof(T);
            memory->peak = std::max(memory->peak, memory->used);
            return result;
        }
        void deallocate(T* value, std::size_t n) noexcept {
            memory->used -= n * sizeof(T);
            std::allocator<T>{}.deallocate(value, n);
        }
        template<class U> bool operator==(const IndexAllocator<U>& other) const noexcept {
            return memory == other.memory;
        }
    };
    // Indices survive vector reallocation. Both entry count and allocator node
    // bytes are bounded; no duplicate row/property snapshot is retained.
    std::set<std::size_t, RowIdentityLess, IndexAllocator<std::size_t>> recovery_index_{
        RowIdentityLess{this}, IndexAllocator<std::size_t>{&index_memory_}};
    std::vector<unsigned char, IndexAllocator<unsigned char>> recovery_seen_{
        IndexAllocator<unsigned char>{&index_memory_}};
    void clear_list_index() noexcept;
    bool index_list_row(std::size_t index) noexcept;
    struct ListRename final {
        FileCommandSnapshot snapshot;
        managed_list::ItemIdentity identity;
    };
    std::optional<ListRename> list_rename_;
    std::optional<core::NavigationRequest> pending_list_focus_restore_;
    bool list_focus_restore_posted_{};
    bool list_focus_restore_timer_used_{};
    bool list_focus_restore_activation_seen_{};
    HWND list_rename_edit_{};
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
    std::size_t list_name_bytes_{};
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
