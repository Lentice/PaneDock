#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <dbt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <new>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>
#include <sherrors.h>
#include <windowsx.h>
#include <wrl/client.h>

#include "app_shell/deferred_messages.h"
#include "app_shell/shell_reentry_guard.h"
#include "app_shell/diagnostic_mode.h"
#include "app_shell/menu_icon.h"
#include "app_shell/pane.h"
#include "app_shell/pane_control_id.h"
#include "app_shell/pane_host.h"
#include "app_shell/pinned_locations_dialog.h"
#include "app_shell/startup_notification.h"
#include "app_shell/tab_overflow.h"
#include "app_shell/transfer_close_dialog.h"
#include "app_shell/window_helpers.h"
#include "app_shell/window_placement.h"
#include "core/key_routing.h"
#include "core/group_transition.h"
#include "core/layout.h"
#include "core/model.h"
#include "core/navigation.h"
#include "core/session.h"
#include "core/shutdown.h"
#include "core/shutdown_coordinator.h"
#include "core/tab_drag.h"
#include "file_operations/file_operations.h"
#include "resource.h"
#include "shell_core/shell_core.h"
#include "app_shell/session_writer.h"
#include "sidebar/sidebar.h"

#include "com_ref_counted.h"

namespace {

using Pane = panedock::app_shell::Pane;
using panedock::app_shell::scaled_value;

// core takes no Windows header, so core::key_routing.h restates the
// virtual-key codes its rules name. This is the seam between the two
// definitions: if a VK_* value ever disagrees, the build fails here rather
// than the routing silently going to the wrong key.
static_assert(panedock::core::kVirtualKeyBack == VK_BACK);
static_assert(panedock::core::kVirtualKeyTab == VK_TAB);
static_assert(panedock::core::kVirtualKeyReturn == VK_RETURN);
static_assert(panedock::core::kVirtualKeyDelete == VK_DELETE);
static_assert(panedock::core::kVirtualKeyLeft == VK_LEFT);
static_assert(panedock::core::kVirtualKeyRight == VK_RIGHT);
static_assert(panedock::core::kVirtualKeyF2 == VK_F2);
static_assert(panedock::core::kVirtualKeyF6 == VK_F6);
static_assert(panedock::core::kVirtualKeyUp == VK_UP);
static_assert(panedock::core::kVirtualKeyF4 == VK_F4);
static_assert(panedock::core::kVirtualKeyF5 == VK_F5);

constexpr wchar_t kWindowClassName[] = L"PaneDockMainWindow";
constexpr wchar_t kSingleInstanceMutexName[] =
    L"PaneDock-SingleInstanceMutex";
constexpr DWORD kSingleInstanceWindowRetryIntervalMs = 50;
constexpr ULONGLONG kSingleInstanceWindowRetryTimeoutMs = 5000;
constexpr DWORD kSingleInstanceActivationTimeoutMs = 250;
// PD-089: the first instance handles this on its own UI thread.
constexpr UINT kActivateExistingInstanceMessage = WM_APP + 50;
// PD-090: run drag-hover callbacks after the WM_TIMER handler returns.
constexpr UINT kDragHoverMessage = WM_APP + 51;
// PD-093: realize non-active startup panes after the first window paint.
constexpr UINT kDeferredRealizeMessage = WM_APP + 52;
// PD-123: keep the main window alive until a PaneDock-owned Shell paste has
// returned from PerformOperations.
constexpr UINT kFileOperationFinishedMessage = WM_APP + 53;
// Defer close until the outermost app-owned Shell call or active OLE drag has
// returned, or until the Closing... caption has had one message-loop turn to
// become visible.
constexpr UINT kDeferredShutdownMessage = WM_APP + 54;
// PD-091: coalesce navigation completions without keeping a polling timer.
constexpr UINT_PTR kSessionSaveTimerId =
    panedock::app_shell::SessionWriter::kTimerId;

// PD-155: coalesce a geometry request that arrives while Shell is pumping the
// message loop during the current layout pass.
constexpr UINT kDeferredLayoutMessage = WM_APP + 55;
// PD-171: replay model-changing commands after an app-owned Shell call.
constexpr UINT kDeferredCommandMessage = WM_APP + 56;
constexpr UINT kDeferredListFocusRestoreMessage = WM_APP + 57;
// Explorer folder options (hidden items, file name extensions) changed: one
// coalesced refresh of the visible panes, posted from WM_SETTINGCHANGE.
constexpr UINT kDeferredShellSettingsMessage = WM_APP + 59;
// After a PaneDock file operation (posted on its completion) and after a volume
// arrived or left (posted from WM_DEVICECHANGE): reconcile the visible panes
// outside any Shell call. See Pane::refresh_unwatched_list / volume_changed.
constexpr UINT kDeferredFileOperationRefreshMessage = WM_APP + 64;
constexpr UINT kDeferredVolumeChangeMessage = WM_APP + 65;
using panedock::core::kSidebarMaximumWidth;
using panedock::core::kSidebarMinimumWidth;
using panedock::core::kMaxPaneCount;
constexpr int kLayoutBarHeight = 44;
constexpr int kLayoutButtonHeight = 30;
constexpr int kLayoutButtonWidth = 30;
// PD-069: 4px spacing scale for app-shell chrome. All values are logical
// pixels and must be passed through scaled_value at their use sites.
constexpr int kSpaceTight = 4;
constexpr int kSpaceSnug = 8;
constexpr int kSpaceBase = 12;
constexpr int kSpaceRoomy = 16;
constexpr int kPaneDividerThickness = 8;
constexpr int kSidebarHeadingHeight = 20;
// The pane chrome metrics (tab strip, navigation row, address bar, footer)
// live in app_shell/pane_chrome_geometry.h with the rect computation that
// consumes them.
// PD-031: rounded light-gray pill drawn behind the address bar EDIT to fake
// a rounded input box (see docs/tickets/PD-031-*.md decision 2). Radius is
// smaller than the design mock's 6px .location radius because the fixed
// kNavigationBarHeight budget (28px@96dpi) does not leave much room for an
// inset that must exceed the radius on every side while still leaving the
// EDIT control tall enough to show text.
constexpr int kAddressBarBackgroundRadius = 4;
// Every command id range, and the decode that owns it, lives in
// app_shell/pane_control_id.h so the four routing/painting sites below cannot
// disagree about where a block starts.
using panedock::app_shell::kCloseAllTabsId;
using panedock::app_shell::kCloseOtherTabsId;
using panedock::app_shell::kCloseTabId;
using panedock::app_shell::kCloseTabsToRightId;
using panedock::app_shell::kDeleteGroupId;
using panedock::app_shell::kDuplicateGroupId;
using panedock::app_shell::kGroupListId;
using panedock::app_shell::kLayoutButtonCount;
using panedock::app_shell::kLayoutButtonIdBase;
using panedock::app_shell::kMoveDownId;
using panedock::app_shell::kMoveUpId;
using panedock::app_shell::kNewGroupId;
using panedock::app_shell::kPinnedMenuMaxLocationCount;
using panedock::app_shell::kRenameGroupId;
using panedock::app_shell::kViewModeOptionCount;
static_assert(panedock::app_shell::kCommandPaneCount ==
              panedock::core::kMaxPaneCount);
static_assert(kViewModeOptionCount ==
              panedock::shell_core::kViewModeOptions.size());
constexpr std::array<std::wstring_view, 2> kPinnedFixedParsingNames{
    L"::{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}",
    L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"};
// Duplicate/Rename/Delete/Move Up/Move Down keep their ids (reused as the
// group list's context-menu command ids, see WM_CONTEXTMENU) but no longer
// get their own footer button; the footer button array shrinks to New Group.
constexpr std::array<int, 1> kButtonIds{kNewGroupId};
constexpr std::array<const wchar_t*, 1> kButtonLabels{L"+ New Group"};
constexpr int kBrandBarHeight = 52;
constexpr std::array<int, kLayoutButtonCount> kLayoutButtonIds{
    kLayoutButtonIdBase, kLayoutButtonIdBase + 1, kLayoutButtonIdBase + 2,
    kLayoutButtonIdBase + 3, kLayoutButtonIdBase + 4,
    kLayoutButtonIdBase + 5, kLayoutButtonIdBase + 6,
    kLayoutButtonIdBase + 7};
constexpr std::array<const wchar_t*, 8> kLayoutButtonLabels{
    L"Single", L"Left / Right", L"Top / Bottom", L"Three",
    L"Two beside One", L"One over Two", L"Two over One", L"Four"};
constexpr std::array<panedock::core::LayoutTemplate, 8> kLayoutTemplates{
    panedock::core::LayoutTemplate::single,
    panedock::core::LayoutTemplate::left_right,
    panedock::core::LayoutTemplate::top_bottom,
    panedock::core::LayoutTemplate::three_pane,
    panedock::core::LayoutTemplate::two_beside_one,
    panedock::core::LayoutTemplate::one_over_two,
    panedock::core::LayoutTemplate::two_over_one,
    panedock::core::LayoutTemplate::four_pane_grid};
constexpr UINT kDragHoverDelayMilliseconds = 800;
constexpr UINT_PTR kDragHoverSidebarTimerId = 0xD034;
constexpr UINT_PTR kDragHoverTabTimerIdBase = 0xD040;

class DragHoverTarget final
    : public panedock::ComRefCounted<DragHoverTarget, IDropTarget>,
      public panedock::app_shell::DragHoverTimer {
public:
    using HitTest = std::function<std::optional<std::size_t>(POINT)>;
    using HoverCallback = std::function<void(std::size_t)>;
    using DragStateCallback = std::function<void(bool)>;

    DragHoverTarget(HWND timer_window, UINT_PTR timer_id, HitTest hit_test,
                    HoverCallback hover_callback,
                    DragStateCallback drag_state_callback)
        : timer_window_(timer_window),
          timer_id_(timer_id),
          hit_test_(std::move(hit_test)),
          hover_callback_(std::move(hover_callback)),
          drag_state_callback_(std::move(drag_state_callback)) {}

    DragHoverTarget(const DragHoverTarget&) = delete;
    DragHoverTarget& operator=(const DragHoverTarget&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                              void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, IID_IDropTarget)) {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*, DWORD, POINTL point,
                                         DWORD* effect) override {
        if (effect == nullptr) return E_INVALIDARG;
        *effect = DROPEFFECT_NONE;
        try {
            begin_drag();
            update_hover({point.x, point.y});
        } catch (...) {
            finish_drag();
            cancel_hover();
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL point,
                                        DWORD* effect) override {
        if (effect == nullptr) return E_INVALIDARG;
        *effect = DROPEFFECT_NONE;
        try {
            update_hover({point.x, point.y});
        } catch (...) {
            cancel_hover();
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        cancel_hover();
        finish_drag();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject*, DWORD, POINTL,
                                    DWORD* effect) override {
        cancel_hover();
        finish_drag();
        if (effect == nullptr) return E_INVALIDARG;
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }

    void timer_expired() noexcept override {
        if (!hover_index_.has_value() || hover_triggered_ || invoking_)
            return;
        hover_triggered_ = true;
        stop_timer();
        pending_generation_ = ++hover_generation_;
        if (!PostMessageW(timer_window_, kDragHoverMessage, timer_id_,
                          static_cast<LPARAM>(pending_generation_))) {
            OutputDebugStringW(
                L"PaneDock: could not queue drag hover callback\n");
        }
    }

    void invoke_hover(UINT_PTR generation) noexcept override {
        if (!hover_index_.has_value() || !hover_triggered_ ||
            pending_generation_ != generation || invoking_)
            return;
        const std::size_t index = *hover_index_;
        invoking_ = true;
        try {
            hover_callback_(index);
        } catch (...) {
            OutputDebugStringW(L"PaneDock: drag hover callback failed\n");
        }
        invoking_ = false;
    }

private:
    friend class panedock::ComRefCounted<DragHoverTarget, IDropTarget>;

    ~DragHoverTarget() {
        finish_drag();
        cancel_hover();
    }

    void begin_drag() {
        if (drag_active_) return;
        drag_active_ = true;
        drag_state_callback_(true);
    }

    void finish_drag() noexcept {
        if (!drag_active_) return;
        drag_active_ = false;
        try {
            drag_state_callback_(false);
        } catch (...) {
            OutputDebugStringW(L"PaneDock: drag state callback failed\n");
        }
    }

    void stop_timer() noexcept {
        if (!timer_running_) return;
        KillTimer(timer_window_, timer_id_);
        timer_running_ = false;
    }

    void cancel_hover() noexcept {
        stop_timer();
        hover_index_.reset();
        hover_triggered_ = false;
    }

    void update_hover(POINT point) {
        const auto hit = hit_test_(point);
        if (!hit.has_value()) {
            cancel_hover();
            return;
        }
        if (hover_index_ == hit) return;

        cancel_hover();
        hover_index_ = hit;
        if (SetTimer(timer_window_, timer_id_, kDragHoverDelayMilliseconds,
                     nullptr) != 0) {
            timer_running_ = true;
        } else {
            hover_index_.reset();
        }
    }

    HWND timer_window_{};
    UINT_PTR timer_id_{};
    HitTest hit_test_;
    HoverCallback hover_callback_;
    DragStateCallback drag_state_callback_;
    std::optional<std::size_t> hover_index_;
    bool timer_running_{false};
    // Guard duplicate DragEnter/DragLeave notifications on one target.
    bool drag_active_{false};
    bool hover_triggered_{false};
    bool invoking_{false};
    UINT_PTR hover_generation_{0};
    UINT_PTR pending_generation_{0};
};

Microsoft::WRL::ComPtr<DragHoverTarget> make_drag_hover_target(
    HWND timer_window, UINT_PTR timer_id, DragHoverTarget::HitTest hit_test,
    DragHoverTarget::HoverCallback hover_callback,
    DragHoverTarget::DragStateCallback drag_state_callback) {
    Microsoft::WRL::ComPtr<DragHoverTarget> target;
    auto* raw = new (std::nothrow)
        DragHoverTarget(timer_window, timer_id, std::move(hit_test),
                        std::move(hover_callback),
                        std::move(drag_state_callback));
    if (raw != nullptr) target.Attach(raw);
    return target;
}

void write_live_list_count(bool diagnostic_mode,
                           std::span<const panedock::app_shell::Pane> panes) noexcept {
    if (!diagnostic_mode) return;
    const auto count = std::count_if(panes.begin(), panes.end(),
        [](const auto& pane) { return pane.realized(); });
    panedock::app_shell::write_diagnostic_line("live_list_count", count);
}


struct LayoutMetrics {
    int minimum_pane_width;
    int minimum_pane_height;
    int divider_thickness;
};

struct Splitter {
    RECT rect;
    std::size_t ratio_index;
    bool vertical;
};

// apply_layout already knew which pane failed and dropped the index on the
// way out, leaving every caller with a bare HRESULT. Keeping it is what lets
// a user-visible warning name the pane.
struct LayoutFailure final {
    HRESULT result;
    std::size_t pane_index;
};

struct AppState;

// Reads every realized pane's live location into the model for SessionWriter.
// The Win32 adapter for ShellReentryGuard's effects.
struct AppReentryEffects final : panedock::app_shell::ShellReentryEffects {
    explicit AppReentryEffects(AppState& state) noexcept : state_(state) {}
    bool window_alive(HWND window) noexcept override {
        return IsWindow(window) != FALSE;
    }
    bool post(HWND window, UINT message, WPARAM wparam,
              LPARAM lparam) noexcept override {
        return PostMessageW(window, message, wparam, lparam) != FALSE;
    }
    HWND shutdown_window() const noexcept override;
    void begin_shutdown_now() noexcept override;

private:
    AppState& state_;
};

// The Win32 adapter for ShutdownCoordinator's effects (PD-216).
struct AppShutdownEffects final : panedock::core::ShutdownEffects {
    explicit AppShutdownEffects(AppState& state) noexcept : state_(state) {}
    void show_closing_caption(bool closing) noexcept override;
    bool has_window() const noexcept override;
    bool window_alive() const noexcept override;
    bool post_deferred_shutdown() noexcept override;
    void show_transfer_prompt() noexcept override;
    void dismiss_transfer_prompt() noexcept override;
    bool save_session() noexcept override;
    bool write_end_session_checkpoint() noexcept override;
    bool ask_keep_open_after_save_failure() noexcept override;
    void destroy_views(bool session_ending) noexcept override;
    void destroy_window() noexcept override;

private:
    AppState& state_;
};

struct AppState : public panedock::app_shell::PaneHost {
    panedock::file_operations::RenameResult rename_item(
        Pane&, const Pane::FileCommandSnapshot&, std::wstring_view) noexcept override;
    panedock::file_operations::RenameResult new_folder(
        Pane&, const Pane::FileCommandSnapshot&) noexcept override;
    void file_drag_changed(bool entering) noexcept override { shutdown_coordinator.drag_changed(entering); }
    bool is_shutting_down() const noexcept override;
    void schedule_session_save() noexcept override;
    const std::string &active_group_id() const noexcept override;
    std::string make_unique_tab_id() const override;
    std::optional<panedock::app_shell::TabStripDragLayout>
    tab_drag_layout(const Pane &pane, HWND strip, int min_width, int max_width,
                    int text_reserve) const override;
    std::span<const panedock::app_shell::PinnedLocation>
    pinned_locations() const noexcept override;
    void pin_location(panedock::core::ShellLocation location) override;
    void manage_pinned_locations() override;
    bool diagnostic_timing_enabled() const noexcept override;
    std::wstring tab_display_text(std::wstring_view parsing_name) override;
    HFONT chrome_font() const noexcept override;
    HWND tooltip() const noexcept override;
    std::optional<LRESULT>
    handle_pane_control_message(Pane &pane, UINT message, WPARAM wparam,
                                LPARAM lparam) override;

    // The legacy names below are read-only references into the reducer
    // state. Keeping them avoids a second pane-wide mechanical rewrite
    // (rejected 2026-09-03); const makes the reducer the only writer (PD-216).
    // The last apply_layout failure, so a warning shown after the fact can
    // still say which pane and which HRESULT.
    std::optional<LayoutFailure> last_layout_failure;
    AppShutdownEffects shutdown_effects{*this};
    panedock::core::ShutdownCoordinator shutdown_coordinator{shutdown_effects};
    AppReentryEffects reentry_effects{*this};
    panedock::app_shell::ShellReentryGuard reentry_guard{
        shutdown_coordinator.sequence(), reentry_effects,
        kDeferredShutdownMessage};
    const bool& quit_requested = shutdown_coordinator.state().quit_requested;
    const bool& closing_ = shutdown_coordinator.state().closing_;
    const bool& shutdown_prompt_active =
        shutdown_coordinator.state().shutdown_prompt_active;
    const bool& shutdown_save_attempted =
        shutdown_coordinator.state().shutdown_save_attempted;
    const bool& shutdown_clean_marker_armed =
        shutdown_coordinator.state().shutdown_clean_marker_armed;
    const bool& main_window_destroyed =
        shutdown_coordinator.state().main_window_destroyed;
    const bool& end_session_pending = shutdown_coordinator.state().end_session_pending;
    const bool& shutdown_deferred = shutdown_coordinator.state().shutdown_deferred;
    const bool& shutdown_message_queued =
        shutdown_coordinator.state().shutdown_message_queued;
    const bool& file_operation_call_active =
        shutdown_coordinator.state().file_operation_call_active;
    const bool& file_operation_in_progress =
        shutdown_coordinator.state().file_operation_in_progress;
    const bool& close_after_file_operation =
        shutdown_coordinator.state().close_after_file_operation;
    const bool& cancel_file_operation =
        shutdown_coordinator.state().cancel_file_operation;
    // PD-093: startup shows the active Shell view first; the posted message
    // realizes the remaining visible panes after the window is interactive.
    bool startup_realize_pending{};
    // WM_CREATE only lays out the frame. Shell work starts after the top-level
    // window has been shown, so a slow active location cannot hide the UI.
    bool startup_frame_only{};
    UINT_PTR startup_realize_generation{};
    panedock::core::ApplicationState application;
    panedock::app_shell::SessionWriter session;
    HWND main_window{nullptr};
    bool diagnostic_mode{};

    panedock::app_shell::TransferCloseDialog transfer_close_dialog;
    // Set by WM_CREATE when the Shell view cannot be opened. Never raised as a
    // modal MessageBox from inside WM_CREATE (that nested loop could dispatch a
    // WM_CLOSE to a half-created HWND); shown by wWinMain after create returns.
    std::wstring startup_error_message;
    // A recoverable startup problem the window can still open with (e.g. one
    // pane's list could not be realized, or tab drag-and-drop failed).
    // Shown by wWinMain after the window is created; never an inside-WM_CREATE
    // modal box. It remains pending until the modeless startup notification is
    // created or until a fatal pre-window path reports it synchronously.
    panedock::app_shell::StartupNotification startup_notification;
    std::wstring& startup_warning_message =
        startup_notification.warning_message();
    struct SidebarDrag final {
        int start_x{};
        int start_width{};
    };
    std::optional<Splitter> splitter_drag;
    std::optional<SidebarDrag> sidebar_drag;
    bool layout_in_progress{};
    bool layout_pending{};
    bool layout_message_queued{};
    bool shell_settings_refresh_queued{};
    struct TabDrag final {
        // The window and the tab identity are the coordinator's; what the
        // gesture *means* lives in core::TabDragState, which is pure and
        // unit-tested (core::update_tab_drag / core::resolve_tab_drop).
        HWND strip{nullptr};
        std::string tab_id;
        panedock::core::TabDragState drag;
    };
    std::optional<TabDrag> tab_drag;
    panedock::sidebar::Sidebar sidebar;
    std::array<HWND, kButtonIds.size()> sidebar_buttons{};
    HWND group_label{nullptr};
    HFONT chrome_font_{nullptr};
    std::array<HWND, kLayoutButtonIds.size()> layout_buttons{};
    HWND owner_draw_hovered_button{nullptr};
    HWND layout_tooltip{nullptr};
    HWND empty_message{nullptr};
    // PD-182: tab_visuals, tab_strip_geometry, tab_hover/scroll-hover
    // indices, folder_context_buttons and the per-pane drag hover target
    // moved into panedock::app_shell::Pane (pure UI state, never
    // persisted). PD-183: explorers/realized/suppress_history_record
    // (Shell view lifetime) moved into Pane too — Pane now owns both the
    // view and its parent HWND, so PD-183 can make destroy ordering a
    // constructor/destructor invariant instead of a call-site convention.
    std::array<panedock::app_shell::Pane, kMaxPaneCount> panes{};
    panedock::app_shell::PinnedLocationsDialog pinned_locations_dialog;
    std::array<std::wstring, kPinnedFixedParsingNames.size()>
        pinned_fixed_labels{};
    std::vector<panedock::app_shell::PinnedLocation>
        pinned_location_menu_items;
    // BrowseToObject may synchronously re-enter navigation_complete while the
    // remaining panes still display the outgoing Group's folders.
    Microsoft::WRL::ComPtr<DragHoverTarget> sidebar_drag_target;
    // finalize_process runs once, from whichever exit gets there first.
    bool ole_finalized{};
    // Memoized ::{GUID} display names for the lifetime of the process
    // (PD-207). Never persisted.
    std::map<std::wstring, std::wstring> display_name_cache;
    // The Group a sidebar context-menu command acts on: the right-clicked
    // row, captured by id when the menu opens. The sidebar selection mirrors
    // the active Group, so it cannot carry the target. Consumed by the
    // command; empty for keyboard/footer paths, which act on the active Group.
    std::optional<std::string> group_menu_target;
};

void drag_state_changed(AppState& state, bool entering) noexcept;

using panedock::app_shell::ShellCallScope;

HWND AppReentryEffects::shutdown_window() const noexcept {
    return state_.main_window;
}

void AppReentryEffects::begin_shutdown_now() noexcept {
    if (IsWindow(state_.main_window))
        state_.shutdown_coordinator.request_close();
}

void defer_shell_reentry_message(HWND window, AppState& state, UINT message,
                                 WPARAM wparam, LPARAM lparam) noexcept {
    // kDragHoverMessage is ours, so this header cannot classify it; its lparam
    // is a hover generation that changes every time, and DragHoverTarget
    // already discards all but the newest.
    const bool replaces =
        message == kDragHoverMessage ||
        panedock::app_shell::deferred_message_replaces_previous(message);
    state.reentry_guard.hold(window, message, wparam, lparam, replaces);
}

bool defer_shell_reentry_mouse_message(HWND window, AppState& state,
                                       UINT message, WPARAM wparam,
                                       LPARAM lparam) noexcept {
    if (!state.reentry_guard.in_shell_call() ||
        (message != WM_LBUTTONDOWN && message != WM_LBUTTONDBLCLK &&
         message != WM_LBUTTONUP && message != WM_CONTEXTMENU))
        return false;
    defer_shell_reentry_message(window, state, message, wparam, lparam);
    return true;
}

void revoke_drag_hover_targets(AppState& state) noexcept {
    state.sidebar.revoke_drag_drop();
    state.sidebar_drag_target.Reset();
    for (auto& pane : state.panes) pane.tab_strip_ui().revoke_drag_hover_target();
}

panedock::core::ShellLocation location(std::wstring parsing_name) {
    return {std::move(parsing_name), {}, {}};
}

panedock::core::ShellLocation default_shell_location() {
    return location(std::wstring(kPinnedFixedParsingNames[1]));
}

// A ::{GUID} display name needs a Shell call that pumps our message loop, and
// one Group switch asks for it once per tab of every pane plus once per
// address bar. The name of a known folder does not change inside one process
// run, so memoize it (PD-207). A lookup that came back empty is not cached --
// that is a transient Shell failure, not an answer.
std::wstring display_text_for_parsing_name(
    AppState& state, std::wstring_view parsing_name) {
    if (!parsing_name.starts_with(L"::")) return std::wstring(parsing_name);

    const std::wstring key(parsing_name);
    if (const auto cached = state.display_name_cache.find(key);
        cached != state.display_name_cache.end())
        return cached->second;

    std::wstring text;
    {
        ShellCallScope shell_call(state.reentry_guard);
        text = panedock::shell_core::display_text_for_parsing_name(
            parsing_name);
    }
    if (!text.empty()) state.display_name_cache.try_emplace(key, text);
    return text;
}

panedock::core::ApplicationState default_application_state() {
    panedock::core::GroupState group;
    group.id = "default";
    group.name = L"Group 1";
    group.layout_template = panedock::core::LayoutTemplate::four_pane_grid;
    group.divider_ratios = panedock::core::default_divider_ratios(
        group.layout_template);
    panedock::core::reserve_panes(group);
    for (std::size_t index = 0; index < kMaxPaneCount; ++index) {
        const std::string suffix = std::to_string(index);
        group.panes.push_back({"pane-" + suffix,
                               {{"tab-" + suffix,
                                 default_shell_location(), {}, {},
                                 true, {}, 0}},
                               "tab-" + suffix});
    }
    group.active_pane_id = group.panes.front().id;

    panedock::core::ApplicationState application;
    application.groups.push_back(std::move(group));
    application.active_group_id = application.groups.front().id;
    application.window_placement = {CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
                                    false};
    assert(panedock::core::is_valid(application));
    return application;
}

// Only behind has_active_group: deleting the last Group leaves none. A
// handler that can run with no Group uses active_pane() instead.
panedock::core::GroupState& active_group(AppState& state) {
    auto* group = panedock::core::find_active_group(state.application);
    assert(group != nullptr);
    return *group;
}

const panedock::core::GroupState& active_group(const AppState& state) {
    const auto* group = panedock::core::find_active_group(state.application);
    assert(group != nullptr);
    return *group;
}

bool has_active_group(const AppState& state) noexcept {
    return !state.application.groups.empty();
}

void rebind_panes(AppState& state) noexcept {
    const std::size_t pane_count =
        has_active_group(state) ? active_group(state).panes.size() : 0;
    for (std::size_t index = 0; index < state.panes.size(); ++index) {
        if (index < pane_count)
            state.panes[index].bind(&active_group(state).panes[index]);
        else
            state.panes[index].unbind();
    }
#ifndef NDEBUG
    for (std::size_t index = 0; index < state.panes.size(); ++index)
        assert((state.panes[index].pane_state() != nullptr) ==
               (index < pane_count));
#endif
}

std::size_t active_pane_index(const panedock::core::GroupState& group) {
    const auto index = panedock::core::find_active_pane_index(group);
    assert(index.has_value());
    return index.value_or(0);
}

// The active Group's active Pane, or null when there is no Group.
Pane* active_pane(AppState& state) noexcept {
    const auto* group = panedock::core::find_active_group(state.application);
    if (group == nullptr) return nullptr;
    const auto index = panedock::core::find_active_pane_index(*group);
    return index && *index < state.panes.size() ? &state.panes[*index] : nullptr;
}

panedock::core::TabState& active_tab(panedock::core::PaneState& pane) {
    const auto tab = std::find_if(
        pane.tabs.begin(), pane.tabs.end(), [&](const auto& candidate) {
            return candidate.id == pane.active_tab_id;
        });
    assert(tab != pane.tabs.end());
    return *tab;
}

const panedock::core::TabState& active_tab(
    const panedock::core::PaneState& pane) {
    const auto tab = std::find_if(
        pane.tabs.begin(), pane.tabs.end(), [&](const auto& candidate) {
            return candidate.id == pane.active_tab_id;
        });
    assert(tab != pane.tabs.end());
    return *tab;
}

// plan_realization takes a snapshot of which panes currently hold a live
// view; Pane owns that flag now (PD-183), so callers collect it here rather
// than plan_realization reaching into Pane itself.
std::array<bool, kMaxPaneCount> realized_flags(const AppState& state) noexcept {
    std::array<bool, kMaxPaneCount> flags{};
    for (std::size_t index = 0; index < kMaxPaneCount; ++index)
        flags[index] = state.panes[index].realized();
    return flags;
}

bool navigate_realized_panes(
    AppState& state, const panedock::core::GroupState& group) noexcept {
    const auto plan = panedock::core::plan_realization(
        group, group.layout_template, realized_flags(state),
        panedock::core::RealizationMode::group_switch);
    panedock::app_shell::ScopedTiming timing(state.diagnostic_mode,
                                             "group_switch_navigate_ms");
    for (const std::size_t pane : plan.navigate) {
        {
            ShellCallScope shell_call(state.reentry_guard);
            (void)state.panes[pane].navigate_to(
                active_tab(group.panes[pane]).location);
        }
        if (state.is_shutting_down()) return false;
    }
    return true;
}

RECT to_win32_rect(const panedock::core::PaneRect& rect) noexcept {
    return {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
}

RECT client_rect(HWND window) noexcept {
    RECT rect{};
    GetClientRect(window, &rect);
    return rect;
}

void add_tooltip(HWND tooltip, HWND owner, UINT_PTR id,
                 const wchar_t* text) noexcept {
    TOOLINFOW info{};
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = owner;
    info.uId = id;
    info.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(tooltip, TTM_ADDTOOLW, 0,
                 reinterpret_cast<LPARAM>(&info));
}

void fill_rounded_rect(HDC dc, const RECT& rect, int radius, COLORREF fill,
                       COLORREF border) noexcept {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = border == CLR_NONE ? nullptr : CreatePen(PS_SOLID, 1, border);
    if (brush != nullptr && (border == CLR_NONE || pen != nullptr)) {
        const HGDIOBJ old_brush = SelectObject(dc, brush);
        const HGDIOBJ old_pen =
            SelectObject(dc, pen != nullptr ? pen : GetStockObject(NULL_PEN));
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius,
                  radius);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
    }
    if (pen != nullptr) DeleteObject(pen);
    if (brush != nullptr) DeleteObject(brush);
}

// These eight take the stored sidebar width rather than the whole AppState.
// Every one of them reads that single int, so the parameter is the
// documentation: nothing else about the application can reach them.
int current_sidebar_width(HWND window, int stored_sidebar_width) noexcept {
    const RECT client = client_rect(window);
    return std::min(static_cast<int>(client.right - client.left),
                    scaled_value(window, stored_sidebar_width));
}

using panedock::app_shell::WindowPositionBatch;

void position_window(WindowPositionBatch* batch, HWND window,
                     const RECT& rect, UINT flags) noexcept {
    if (batch != nullptr) {
        batch->position(window, nullptr, rect, flags);
        return;
    }
    (void)SetWindowPos(window, nullptr, rect.left, rect.top,
                       rect.right - rect.left, rect.bottom - rect.top, flags);
}


void draw_layout_glyph(HDC dc, RECT rect, std::size_t index,
                       COLORREF color) noexcept {
    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    const int size = std::min(16, std::max(1, std::min(width, height) - 8));
    RECT glyph{rect.left + (rect.right - rect.left - size) / 2,
               rect.top + (rect.bottom - rect.top - size) / 2,
               rect.left + (rect.right - rect.left + size) / 2,
               rect.top + (rect.bottom - rect.top + size) / 2};
    HPEN frame_pen = CreatePen(PS_SOLID, 1, color);
    if (frame_pen != nullptr) {
        const HGDIOBJ old_pen = SelectObject(dc, frame_pen);
        const HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        const int radius = std::max(1, size / 6);
        RoundRect(dc, glyph.left, glyph.top, glyph.right, glyph.bottom, radius,
                  radius);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(frame_pen);
    }

    HPEN pen = CreatePen(PS_SOLID, 1, color);
    if (pen == nullptr) return;
    const HGDIOBJ previous = SelectObject(dc, pen);
    const int mid_x = glyph.left + (glyph.right - glyph.left) / 2;
    const int mid_y = glyph.top + (glyph.bottom - glyph.top) / 2;
    switch (index) {
        case 1:
            MoveToEx(dc, mid_x, glyph.top, nullptr);
            LineTo(dc, mid_x, glyph.bottom);
            break;
        case 2:
            MoveToEx(dc, glyph.left, mid_y, nullptr);
            LineTo(dc, glyph.right, mid_y);
            break;
        case 3:
            MoveToEx(dc, mid_x, glyph.top, nullptr);
            LineTo(dc, mid_x, glyph.bottom);
            MoveToEx(dc, mid_x, mid_y, nullptr);
            LineTo(dc, glyph.right, mid_y);
            break;
        case 4:
            MoveToEx(dc, mid_x, glyph.top, nullptr);
            LineTo(dc, mid_x, glyph.bottom);
            MoveToEx(dc, glyph.left, mid_y, nullptr);
            LineTo(dc, mid_x, mid_y);
            break;
        case 5:
            MoveToEx(dc, glyph.left, mid_y, nullptr);
            LineTo(dc, glyph.right, mid_y);
            MoveToEx(dc, mid_x, mid_y, nullptr);
            LineTo(dc, mid_x, glyph.bottom);
            break;
        case 6:
            MoveToEx(dc, glyph.left, mid_y, nullptr);
            LineTo(dc, glyph.right, mid_y);
            MoveToEx(dc, mid_x, glyph.top, nullptr);
            LineTo(dc, mid_x, mid_y);
            break;
        case 7:
            MoveToEx(dc, mid_x, glyph.top, nullptr);
            LineTo(dc, mid_x, glyph.bottom);
            MoveToEx(dc, glyph.left, mid_y, nullptr);
            LineTo(dc, glyph.right, mid_y);
            break;
        default:
            break;
    }
    SelectObject(dc, previous);
    DeleteObject(pen);
}

void draw_layout_button(const DRAWITEMSTRUCT& item,
                        std::size_t index, bool checked,
                        bool fallback_hovered) noexcept {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool hovered = (item.itemState & ODS_HOTLIGHT) != 0 ||
                         fallback_hovered;
    const COLORREF background = disabled
                                    ? RGB(245, 247, 249)
                                    : checked   ? RGB(37, 99, 235)
                                    : hovered   ? RGB(226, 232, 240)
                                                : RGB(248, 250, 252);
    const COLORREF glyph = disabled
                               ? RGB(148, 163, 184)
                               : checked ? RGB(255, 255, 255)
                                         : RGB(100, 116, 139);
    RECT button = item.rcItem;
    InflateRect(&button, -1, -1);
    HBRUSH fill = CreateSolidBrush(background);
    if (fill != nullptr) {
        FillRect(item.hDC, &button, fill);
        DeleteObject(fill);
    }
    if (checked) {
        HBRUSH border = CreateSolidBrush(RGB(29, 78, 216));
        if (border != nullptr) {
            FrameRect(item.hDC, &button, border);
            DeleteObject(border);
        }
    }
    draw_layout_glyph(item.hDC, item.rcItem, index, glyph);
}

void draw_layout_segment_background(HDC dc, RECT rect, UINT dpi) noexcept {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    const int radius = std::max(
        1, MulDiv(kAddressBarBackgroundRadius, static_cast<int>(dpi), 96));
    fill_rounded_rect(dc, rect, radius, RGB(251, 252, 253),
                      RGB(217, 225, 234));
}

void draw_sidebar_action_button(const DRAWITEMSTRUCT& item,
                                const wchar_t* label,
                                bool tracked_hovered) noexcept {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool hovered = !disabled &&
                         ((item.itemState & ODS_HOTLIGHT) != 0 ||
                          tracked_hovered);
    const COLORREF border = disabled ? RGB(232, 235, 239) : RGB(223, 229, 236);
    const COLORREF text_color = disabled ? RGB(180, 188, 199) : RGB(82, 96, 117);
    const int radius =
        std::max(4, static_cast<int>(item.rcItem.bottom - item.rcItem.top) / 4);
    fill_rounded_rect(item.hDC, item.rcItem, radius,
                      hovered ? RGB(242, 245, 248) : RGB(255, 255, 255),
                      border);

    const HFONT font = reinterpret_cast<HFONT>(
        SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font =
        font != nullptr ? SelectObject(item.hDC, font) : nullptr;
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text_color);
    RECT text_rect = item.rcItem;
    DrawTextW(item.hDC, label, -1, &text_rect,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    if (old_font != nullptr) SelectObject(item.hDC, old_font);
}

RECT pane_area(HWND window, int stored_sidebar_width) noexcept {
    RECT area = client_rect(window);
    area.left = std::min(
        area.right, area.left + current_sidebar_width(window,
                                                      stored_sidebar_width));
    area.top = std::min(area.bottom,
                        area.top + scaled_value(window, kLayoutBarHeight));
    return area;
}

LayoutMetrics layout_metrics(HWND window) noexcept {
    const double scale = static_cast<double>(GetDpiForWindow(window)) / 96.0;
    const auto scaled = [scale](int value) {
        return std::max(1, static_cast<int>(std::lround(value * scale)));
    };
    return {scaled(panedock::core::kMinimumPaneWidth),
            scaled(panedock::core::kMinimumPaneHeight),
            scaled(kPaneDividerThickness)};
}

bool sidebar_boundary_at_point(HWND window, int stored_sidebar_width,
                               POINT point) noexcept {
    const RECT client = client_rect(window);
    return point.y >= client.top && point.y < client.bottom &&
           panedock::core::sidebar_boundary_contains(
               point.x,
               client.left + current_sidebar_width(window,
                                                   stored_sidebar_width),
               layout_metrics(window).divider_thickness, client.left,
               client.right);
}

RECT pane_content_area(HWND window, int stored_sidebar_width) noexcept {
    const RECT area = pane_area(window, stored_sidebar_width);
    const LayoutMetrics metrics = layout_metrics(window);
    const auto content = panedock::core::pane_content_rect(
        {area.left, area.top, area.right - area.left, area.bottom - area.top},
        scaled_value(window, kSpaceRoomy), metrics.minimum_pane_width,
        metrics.minimum_pane_height);
    return to_win32_rect(content);
}

std::vector<panedock::core::PaneRect> layout_rects(
    HWND window, int stored_sidebar_width,
    const panedock::core::GroupState& group) {
    const RECT client = pane_content_area(window, stored_sidebar_width);
    const LayoutMetrics metrics = layout_metrics(window);
    auto rects = panedock::core::compute_layout_rects(
        client.right - client.left, client.bottom - client.top,
        group.layout_template, group.divider_ratios,
        metrics.minimum_pane_width, metrics.minimum_pane_height,
        metrics.divider_thickness);
    for (auto& rect : rects) {
        rect.x += client.left;
        rect.y += client.top;
    }
    return rects;
}

// The divider geometry itself is pure and unit-tested in
// core::compute_splitter_rects; this only supplies the window's metrics and
// converts to RECT.
std::vector<Splitter> splitters(HWND window, int stored_sidebar_width,
                                const panedock::core::GroupState& group) {
    const auto rects = layout_rects(window, stored_sidebar_width, group);
    const auto bars = panedock::core::compute_splitter_rects(
        rects, group.layout_template,
        layout_metrics(window).divider_thickness);
    std::vector<Splitter> result;
    result.reserve(bars.size());
    for (const auto& bar : bars)
        result.push_back({to_win32_rect(bar.rect), bar.ratio_index,
                          bar.vertical});
    return result;
}

std::optional<Splitter> splitter_at_point(
    HWND window, int stored_sidebar_width,
    const panedock::core::GroupState& group, POINT point) {
    for (const Splitter& splitter :
         splitters(window, stored_sidebar_width, group)) {
        if (PtInRect(&splitter.rect, point)) return splitter;
    }
    return std::nullopt;
}

std::wstring tab_display_text(AppState& state,
                              std::wstring_view parsing_name) {
    const std::size_t separator = parsing_name.find_last_of(L"\\/");
    const std::wstring_view last_segment =
        separator == std::wstring::npos ? parsing_name
                                        : parsing_name.substr(separator + 1);
    // A namespace parsing name ("::{GUID}", "::{GUID}\::{GUID}", a device
    // path below This PC) has no readable last segment, so ask the Shell for
    // the item's display name. display_text_for_parsing_name memoizes it for
    // the process; it is display text only, never identity.
    if (parsing_name.starts_with(L"::")) {
        std::wstring text = display_text_for_parsing_name(state, parsing_name);
        if (!text.empty() && text != parsing_name) return text;
        if (!last_segment.empty() && !last_segment.starts_with(L"::"))
            return std::wstring(last_segment);
        return text.empty() ? std::wstring(parsing_name) : text;
    }
    if (last_segment.empty()) return std::wstring(parsing_name);
    return std::wstring(last_segment);
}

void refresh_tab_strips(AppState& state) {
    for (auto& pane : state.panes) pane.tab_strip_ui().refresh();
}

void refresh_sidebar(AppState& state) {
    std::vector<panedock::sidebar::GroupSummary> summaries;
    summaries.reserve(state.application.groups.size());
    std::optional<std::size_t> active_index;
    for (std::size_t index = 0; index < state.application.groups.size();
         ++index) {
        const auto& group = state.application.groups[index];
        const std::size_t visible_panes =
            (std::min)(panedock::core::pane_count(group.layout_template),
                       group.panes.size());
        const std::size_t tab_count = std::accumulate(
            group.panes.begin(), group.panes.begin() + visible_panes,
            std::size_t{0},
            [](std::size_t total, const panedock::core::PaneState& pane) {
                return total + pane.tabs.size();
            });
        summaries.push_back({group.id, group.name, visible_panes, tab_count});
        if (group.id == state.application.active_group_id) active_index = index;
    }
    state.sidebar.set_groups(summaries);
    if (active_index.has_value()) state.sidebar.set_selected_index(*active_index);

    // Duplicate/Rename/Delete/Move Up/Move Down live on the list's context
    // menu now (see WM_CONTEXTMENU); the footer only keeps New Group, which
    // is always available.
    EnableWindow(state.sidebar_buttons[0], TRUE);
}

void layout_sidebar(HWND window, AppState& state,
                    WindowPositionBatch* batch = nullptr) noexcept {
    const RECT client = client_rect(window);
    const int width = current_sidebar_width(window, state.application.sidebar_width);
    const int margin = scaled_value(window, kSpaceSnug);
    const int gap = scaled_value(window, kSpaceTight);
    const int brand_height = scaled_value(window, kBrandBarHeight);
    const int heading_height = scaled_value(window, kSidebarHeadingHeight);
    const int button_height = scaled_value(window, 28);
    const int controls_height = static_cast<int>(state.sidebar_buttons.size()) *
                                    button_height +
                                static_cast<int>(state.sidebar_buttons.size() - 1) * gap;
    position_window(batch, state.group_label,
                    RECT{margin, brand_height + margin,
                         margin + std::max(0, width - 2 * margin),
                         brand_height + margin + heading_height},
                    SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(state.group_label, SW_SHOW);
    const int list_top = brand_height + margin + heading_height + gap;
    RECT list_rect{margin, list_top, std::max(margin, width - margin),
                   std::max(list_top, static_cast<int>(client.bottom) - margin -
                                             controls_height - gap)};
    position_window(batch, state.sidebar.window(), list_rect,
                    SWP_NOZORDER | SWP_NOACTIVATE);
    state.sidebar.set_dpi(GetDpiForWindow(window));
    int y = list_rect.bottom + gap;
    for (HWND button : state.sidebar_buttons) {
        position_window(batch, button,
                        RECT{margin, y, margin + std::max(0, width - 2 * margin),
                             y + button_height},
                        SWP_NOZORDER | SWP_NOACTIVATE);
        y += button_height + gap;
    }
    const RECT panes = pane_area(window, state.application.sidebar_width);
    position_window(batch, state.empty_message, panes,
                    SWP_NOZORDER | SWP_NOACTIVATE);
}

void layout_header(HWND window, AppState& state,
                   WindowPositionBatch* batch = nullptr,
                   bool update_selection = true) noexcept {
    const RECT client = client_rect(window);
    const int sidebar_width = current_sidebar_width(window, state.application.sidebar_width);
    const int margin = scaled_value(window, kSpaceBase);
    const int segment_gap = scaled_value(window, 1);
    const int header_height = std::min(
        scaled_value(window, kLayoutBarHeight),
        std::max(0, static_cast<int>(client.bottom - client.top)));
    const int button_height = std::min(
        scaled_value(window, kLayoutButtonHeight), header_height);
    // Right-alignment, the shrink-to-fit width and the no-overlap fallback
    // are pure and unit-tested in core::compute_layout_button_strip.
    const auto strip = panedock::core::compute_layout_button_strip(
        static_cast<int>(client.right), sidebar_width, margin, segment_gap,
        scaled_value(window, kLayoutButtonWidth),
        static_cast<int>(kLayoutButtonIds.size()));
    const int button_width = strip.button_width;
    int x = strip.x;
    for (HWND button : state.layout_buttons) {
        position_window(
            batch, button,
            RECT{x, std::max(0, (header_height - button_height) / 2),
                 x + button_width,
                 std::max(0, (header_height - button_height) / 2) +
                     button_height},
            SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(button, SW_SHOW);
        x += button_width + segment_gap;
    }
    if (!update_selection) return;
    const bool enabled = has_active_group(state);
    const auto current = enabled ? active_group(state).layout_template
                                 : panedock::core::LayoutTemplate::single;
    for (std::size_t index = 0; index < state.layout_buttons.size(); ++index) {
        EnableWindow(state.layout_buttons[index], enabled);
        SendMessageW(state.layout_buttons[index], BM_SETCHECK,
                     kLayoutTemplates[index] == current ? BST_CHECKED
                                                         : BST_UNCHECKED,
                     0);
        InvalidateRect(state.layout_buttons[index], nullptr, FALSE);
    }
}

// PD-072: DEFAULT_GUI_FONT is a Win16 stock object that resolves to a serif
// CJK face on Chinese Windows. Build one owned font from the current
// per-window message font, pinning Latin text to Segoe UI while preserving
// the system's height, weight and other metrics.
HFONT ui_font(HWND window) noexcept {
    if (window == nullptr) return nullptr;
    const UINT dpi = GetDpiForWindow(window);
    if (dpi == 0) return nullptr;

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                                   &metrics, 0, dpi) == FALSE)
        return nullptr;

    const LOGFONTW fallback = metrics.lfMessageFont;
    LOGFONTW requested = fallback;
    if (wcscpy_s(requested.lfFaceName, LF_FACESIZE, L"Segoe UI") != 0)
        return CreateFontIndirectW(&fallback);
    requested.lfCharSet = DEFAULT_CHARSET;
    requested.lfQuality = CLEARTYPE_QUALITY;

    HFONT font = CreateFontIndirectW(&requested);
    if (font == nullptr) return CreateFontIndirectW(&fallback);

    bool has_requested_face = true;
    HDC dc = GetDC(window);
    if (dc != nullptr) {
        const HGDIOBJ old_font = SelectObject(dc, font);
        if (old_font != nullptr && old_font != HGDI_ERROR) {
            wchar_t actual_face[LF_FACESIZE]{};
            const int length = GetTextFaceW(
                dc, LF_FACESIZE, actual_face);
            has_requested_face =
                length > 0 && lstrcmpiW(actual_face, L"Segoe UI") == 0;
            SelectObject(dc, old_font);
        }
        ReleaseDC(window, dc);
    }
    if (has_requested_face) return font;
    DeleteObject(font);
    return CreateFontIndirectW(&fallback);
}

void set_ui_font(HWND control, HFONT font) noexcept {
    if (control != nullptr && font != nullptr)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void apply_ui_font(AppState& state) noexcept {
    const HFONT font = state.chrome_font_;
    if (font == nullptr) return;
    set_ui_font(state.sidebar.window(), font);
    set_ui_font(state.group_label, font);
    for (HWND button : state.sidebar_buttons) set_ui_font(button, font);
    for (HWND button : state.layout_buttons) set_ui_font(button, font);
    set_ui_font(state.empty_message, font);
    for (auto& chrome : state.panes) {
        chrome.apply_font(font);
    }
    state.pinned_locations_dialog.apply_font(font);
    state.startup_notification.apply_font(font);
}

void refresh_ui_font(HWND window, AppState& state) noexcept {
    const HFONT next = ui_font(window);
    if (next == nullptr) return;
    const HFONT previous = state.chrome_font_;
    state.chrome_font_ = next;
    apply_ui_font(state);
    if (previous != nullptr) DeleteObject(previous);
}

void release_ui_font(AppState& state) noexcept {
    if (state.chrome_font_ != nullptr) {
        DeleteObject(state.chrome_font_);
        state.chrome_font_ = nullptr;
    }
}

HICON& brand_icon(HWND window) noexcept {
    static HICON icon = nullptr;
    if (icon == nullptr && window != nullptr) {
        const int icon_size = scaled_value(window, 28);
        icon = static_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON, icon_size, icon_size, LR_DEFAULTCOLOR));
    }
    return icon;
}

HFONT& brand_font(HWND window) noexcept {
    static HFONT font = nullptr;
    if (font == nullptr && window != nullptr) {
        HFONT base = ui_font(window);
        if (base == nullptr) return font;
        LOGFONTW logfont{};
        if (GetObjectW(base, sizeof(logfont), &logfont) == 0) {
            DeleteObject(base);
            return font;
        }
        DeleteObject(base);
        logfont.lfWeight = FW_BOLD;
        font = CreateFontIndirectW(&logfont);
    }
    return font;
}

void release_brand_resources() noexcept {
    HICON& icon = brand_icon(nullptr);
    if (icon != nullptr) {
        DestroyIcon(icon);
        icon = nullptr;
    }
    HFONT& font = brand_font(nullptr);
    if (font != nullptr) {
        DeleteObject(font);
        font = nullptr;
    }
}

void draw_brand_bar(HWND window, HDC dc, RECT rect) noexcept {
    HBRUSH background = CreateSolidBrush(RGB(251, 252, 254));
    if (background != nullptr) {
        FillRect(dc, &rect, background);
        DeleteObject(background);
    }
    const int icon_size = scaled_value(window, 28);
    const int icon_margin = scaled_value(window, kSpaceRoomy);
    const RECT icon{rect.left + icon_margin,
                    rect.top + ((rect.bottom - rect.top) - icon_size) / 2,
                    rect.left + icon_margin + icon_size,
                    rect.top + ((rect.bottom - rect.top) - icon_size) / 2 +
                        icon_size};
    const HICON app_icon = brand_icon(window);
    if (app_icon != nullptr) {
        DrawIconEx(dc, icon.left, icon.top, app_icon, icon_size, icon_size, 0,
                   nullptr, DI_NORMAL);
    }

    RECT title{icon.right + scaled_value(window, kSpaceBase), rect.top,
              rect.right - scaled_value(window, kSpaceRoomy), rect.bottom};
    const HFONT font = brand_font(window);
    const HGDIOBJ old_font = font != nullptr ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(30, 41, 59));
    DrawTextW(dc, L"PaneDock", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (old_font != nullptr) SelectObject(dc, old_font);
}

void paint_client_background(HWND window, HDC dc, int sidebar_width,
                             std::span<const HWND> layout_buttons) noexcept {
    const RECT client = client_rect(window);
    HBRUSH canvas = CreateSolidBrush(RGB(243, 246, 249));
    if (canvas != nullptr) {
        FillRect(dc, &client, canvas);
        DeleteObject(canvas);
    }

    RECT sidebar{client.left, client.top, client.left + sidebar_width,
                 client.bottom};
    HBRUSH sidebar_brush = CreateSolidBrush(RGB(251, 252, 254));
    if (sidebar_brush != nullptr) {
        FillRect(dc, &sidebar, sidebar_brush);
        DeleteObject(sidebar_brush);
    }

    const RECT brand_bar{sidebar.left, sidebar.top, sidebar.right,
                         std::min(sidebar.bottom,
                                  sidebar.top +
                                      scaled_value(window, kBrandBarHeight))};
    draw_brand_bar(window, dc, brand_bar);

    RECT header{client.left + sidebar_width, client.top, client.right,
                std::min(client.bottom,
                         client.top + scaled_value(window, kLayoutBarHeight))};
    FillRect(dc, &header, GetSysColorBrush(COLOR_WINDOW));

    RECT layout_group{};
    RECT last_layout_button{};
    if (!layout_buttons.empty() &&
        GetWindowRect(layout_buttons.front(), &layout_group) &&
        GetWindowRect(layout_buttons.back(), &last_layout_button)) {
        MapWindowPoints(nullptr, window,
                        reinterpret_cast<POINT*>(&layout_group), 2);
        MapWindowPoints(nullptr, window,
                        reinterpret_cast<POINT*>(&last_layout_button), 2);
        layout_group.right = last_layout_button.right;
        draw_layout_segment_background(dc, layout_group,
                                       GetDpiForWindow(window));
    }

    RECT divider{header.left, header.bottom - scaled_value(window, 1),
                 header.right, header.bottom};
    HBRUSH divider_brush = CreateSolidBrush(RGB(223, 229, 236));
    if (divider_brush != nullptr) {
        FillRect(dc, &divider, divider_brush);
        DeleteObject(divider_brush);
    }

}

void cancel_session_save_timer(AppState& state) noexcept {
    state.session.cancel_timer(state.main_window);
}


void append_startup_warning(AppState& state, std::wstring_view warning) {
    state.startup_notification.append_warning(warning);
}

// The pane index and HRESULT of the last layout failure, for a warning that
// is shown after apply_layout has already returned.
std::optional<std::size_t> last_failed_pane(const AppState& state) noexcept {
    if (!state.last_layout_failure.has_value()) return std::nullopt;
    return state.last_layout_failure->pane_index;
}

// Two audiences, two amounts of detail. The debugger channel gets the call
// site and the folder that failed, which is what actually attributes a
// third-party Shell extension fault; the user-visible notification gets only
// the short detail line (see format_shell_failure_detail).
void report_shell_failure(const AppState& state, const wchar_t* site,
                          HRESULT result) noexcept try {
    std::wstring message = L"PaneDock: list realization failed site=";
    message += site;
    message += L' ';
    message += panedock::app_shell::format_shell_failure_detail(
        result, last_failed_pane(state));
    if (const auto pane = last_failed_pane(state);
        pane.has_value() && has_active_group(state)) {
        const auto& panes = active_group(state).panes;
        if (*pane < panes.size()) {
            message += L" location=";
            message += active_tab(panes[*pane]).location.parsing_name;
        }
    }
    message += L'\n';
    OutputDebugStringW(message.c_str());
} catch (...) {
    // Never let diagnostics be the thing that breaks a message handler.
    OutputDebugStringW(L"PaneDock: list realization failed\n");
}

// The call-site vocabulary the release checks police (which paths may write
// synchronously and which only schedule); SessionWriter owns the rules.
bool save_now(AppState& state, bool clean_shutdown = false) noexcept {
    return state.session.save_now(clean_shutdown);
}

void schedule_session_save(AppState& state) noexcept {
    state.session.schedule();
}

void rebuild_pinned_location_menu(AppState& state);

void apply_pinned_locations_dialog_result(AppState& state) noexcept {
    auto result = state.pinned_locations_dialog.take_result();
    if (!result.has_value() || *result == state.application.pinned_locations)
        return;
    state.application.pinned_locations = std::move(*result);
    rebuild_pinned_location_menu(state);
    schedule_session_save(state);
}

void show_pinned_locations_manager(HWND owner, AppState& state) {
    std::vector<std::wstring> display_labels;
    display_labels.reserve(state.application.pinned_locations.size());
    for (const auto& pinned : state.application.pinned_locations)
        display_labels.push_back(
            display_text_for_parsing_name(state, pinned.parsing_name));
    state.pinned_locations_dialog.show(owner, state.application.pinned_locations,
                                      std::move(display_labels),
                                      state.chrome_font_);
}

void rebuild_pinned_location_menu(AppState& state) {
    auto &items = state.pinned_location_menu_items;
    items.clear();
    items.reserve(panedock::app_shell::kPinnedMenuFixedLocationCount +
                  (std::min)(state.application.pinned_locations.size(),
                             static_cast<std::size_t>(
                                 kPinnedMenuMaxLocationCount)));
    for (std::size_t index = 0; index < kPinnedFixedParsingNames.size();
         ++index) {
        items.push_back({location(std::wstring(kPinnedFixedParsingNames[index])),
                         state.pinned_fixed_labels[index]});
    }
    const std::size_t count =
        (std::min)(state.application.pinned_locations.size(),
                   static_cast<std::size_t>(kPinnedMenuMaxLocationCount));
    for (std::size_t index = 0; index < count; ++index) {
        const auto &pinned = state.application.pinned_locations[index];
        items.push_back(
            {pinned, display_text_for_parsing_name(state, pinned.parsing_name)});
    }
}

// OLE revocation and Shell interfaces can re-enter even without a hosted view.
// Keep teardown inside the shared gate; Pane destroys its list before parents.
void destroy_panes(AppState& state) noexcept {
    for (auto& chrome : state.panes) {
        ShellCallScope shell_call(state.reentry_guard);
        chrome.destroy();
    }
    write_live_list_count(state.diagnostic_mode, state.panes);
}

class LayoutPassScope final {
public:
    LayoutPassScope(HWND window, AppState& state) noexcept
        : window_(window), state_(state) {
        state_.layout_in_progress = true;
    }

    ~LayoutPassScope() noexcept {
        state_.layout_in_progress = false;
        if (!state_.layout_pending || state_.layout_message_queued ||
            state_.is_shutting_down() || state_.main_window == nullptr)
            return;
        state_.layout_message_queued = true;
        if (!PostMessageW(window_, kDeferredLayoutMessage, 0, 0)) {
            state_.layout_message_queued = false;
            OutputDebugStringW(
                L"PaneDock: could not queue deferred layout\n");
        }
    }

    LayoutPassScope(const LayoutPassScope&) = delete;
    LayoutPassScope& operator=(const LayoutPassScope&) = delete;

private:
    HWND window_;
    AppState& state_;
};

HRESULT apply_layout(HWND window, AppState& state,
                     bool realize_deferred_panes = false,
                     bool recompute_content = true) {
    // OLE/Shell teardown can pump; nested layout must not recreate content while the
    // parent HWND is being destroyed (§9.4). Guard the shared entry, not every
    // caller.
    if (state.is_shutting_down()) return E_ABORT;
    if (state.layout_in_progress) {
        state.layout_pending = true;
        return S_OK;
    }
    LayoutPassScope layout_scope(window, state);
    WindowPositionBatch main_positions;
    // DeferWindowPos requires every window in a batch to share one parent:
    // main-window chrome/pane HWNDs, each pane's children, and each pane's
    // managed list (a child of its list container) therefore use separate
    // batches committed in this pass.
    std::array<std::optional<WindowPositionBatch>, kMaxPaneCount>
        pane_positions;
    std::array<std::optional<WindowPositionBatch>, kMaxPaneCount>
        list_positions;
    layout_sidebar(window, state, &main_positions);
    layout_header(window, state, &main_positions, recompute_content);
    if (!has_active_group(state)) {
        for (std::size_t index = 0; index < state.panes.size(); ++index) {
            {
                ShellCallScope shell_call(state.reentry_guard);
                state.panes[index].derealize();
            }
            if (state.is_shutting_down()) return E_ABORT;
            state.panes[index].set_visible(false);
        }
        ShowWindow(state.empty_message, SW_SHOW);
        for (auto& chrome : state.panes)
            chrome.laid_out_pane_rect().reset();
        (void)main_positions.commit();
        InvalidateRect(window, nullptr, FALSE);
        if (recompute_content) write_live_list_count(state.diagnostic_mode, state.panes);
        return S_OK;
    }
    ShowWindow(state.empty_message, SW_HIDE);
    // This is the one place that holds a GroupState& across steps that pump
    // the message loop (realization does Shell calls), which
    // perform_group_transition deliberately does not. It is sound only
    // because add_group/delete_group can be reached solely through WM_COMMAND,
    // and the main window defers WM_COMMAND while a Shell call is in progress --
    // nothing can grow or shrink the groups vector inside this pass and
    // relocate what `group` points at. If that deferral ever narrows, this
    // reference has to be looked up per step instead. The assert at the end of
    // the pass is what catches that regression.
    auto& group = active_group(state);
    [[maybe_unused]] const std::size_t group_count_on_entry =
        state.application.groups.size();
    const auto rects = layout_rects(window, state.application.sidebar_width, group);
    const auto realization_mode =
        state.startup_frame_only
            ? panedock::core::RealizationMode::startup_frame
            : (state.startup_realize_pending && !realize_deferred_panes
                   ? panedock::core::RealizationMode::startup_deferred
                   : panedock::core::RealizationMode::normal);
    const auto realization_plan = panedock::core::plan_realization(
        group, group.layout_template, realized_flags(state), realization_mode);
    const auto plan_contains = [](const std::vector<std::size_t>& indexes,
                                  std::size_t index) noexcept {
        return std::find(indexes.begin(), indexes.end(), index) !=
               indexes.end();
    };
    const UINT dpi = GetDpiForWindow(window);
    {
        const std::size_t active = active_pane_index(group);
        for (std::size_t index = 0; index < state.panes.size(); ++index)
            state.panes[index].set_chrome_active(index == active);
    }
    std::optional<LayoutFailure> first_failure;
    std::array<bool, kMaxPaneCount> changed_panes{};
    std::array<Pane::LayoutResult, kMaxPaneCount> pane_layouts{};
    std::array<std::optional<RECT>, kMaxPaneCount> pending_shell_rects{};
    std::array<bool, kMaxPaneCount> shell_positions_deferred{};
    for (std::size_t index = 0; index < state.panes.size(); ++index) {
        auto& chrome = state.panes[index];
        const bool visible =
            index < panedock::core::pane_count(group.layout_template);
        if (visible) {
            pane_positions[index].emplace();
            const auto layout = chrome.stage_layout(
                to_win32_rect(rects[index]), dpi, main_positions,
                *pane_positions[index]);
            pane_layouts[index] = layout;
            const bool pane_geometry_changed = layout.changed;
            changed_panes[index] = pane_geometry_changed;
            const RECT local_rect = layout.shell_rect;
            if (plan_contains(realization_plan.realize, index)) {
                HRESULT hr = E_UNEXPECTED;
                {
                    ShellCallScope shell_call(state.reentry_guard);
                    hr = chrome.realize(
                        local_rect, active_tab(group.panes[index]).location);
                }
                if (state.is_shutting_down()) return E_ABORT;
                if (FAILED(hr)) {
                    if (!first_failure.has_value())
                        first_failure = LayoutFailure{hr, index};
                    continue;
                }
                if (const HRESULT configured = chrome.configure_realized_view();
                    FAILED(configured))
                    return configured;
            } else if (pane_geometry_changed) {
                pending_shell_rects[index] = local_rect;
                list_positions[index].emplace();
                if (list_positions[index]->active()) {
                    shell_positions_deferred[index] = true;
                    chrome.set_shell_rect(
                        local_rect, &*list_positions[index]);
                } else {
                    chrome.set_shell_rect(local_rect, nullptr);
                }
                if (state.is_shutting_down()) return E_ABORT;
            }
            if (recompute_content) {
                state.panes[index].refresh_status_bar();
                if (state.is_shutting_down()) return E_ABORT;
            }
            // Pane owns the outer-rect cache; the native child batches
            // are committed below before regions are applied.
        } else {
            if (plan_contains(realization_plan.derealize, index)) {
                {
                    ShellCallScope shell_call(state.reentry_guard);
                    chrome.derealize();
                }
                if (state.is_shutting_down()) return E_ABORT;
            }
            chrome.set_visible(false);
        }
        chrome.set_shell_visible(visible);
        if (state.is_shutting_down()) return E_ABORT;
    }
    const bool committed = main_positions.commit();
    for (auto& pane_position : pane_positions) {
        if (pane_position.has_value()) (void)pane_position->commit();
    }
    // The tab strip's custom add/scroll-button geometry depends on its new
    // client width. Recompute only after the app batch has committed; before
    // then GetClientRect still describes the previous live-resize frame.
    const std::size_t visible_panes =
        panedock::core::pane_count(group.layout_template);
    for (std::size_t index = 0; index < state.panes.size(); ++index) {
        // PaneTabStrip::refresh() already applies the item size whenever the
        // tabs themselves changed, so recompute_content would only repeat it
        // (PD-207). Geometry is the one thing refresh() does not know about.
        if (index < visible_panes && changed_panes[index])
            state.panes[index].tab_strip_ui().apply_item_size();
    }
    for (std::size_t index = 0; index < state.panes.size(); ++index) {
        if (!list_positions[index].has_value()) continue;
        const bool list_committed = list_positions[index]->commit();
        if (!list_committed && shell_positions_deferred[index]) {
            state.panes[index].set_shell_rect(
                *pending_shell_rects[index], nullptr);
            if (state.is_shutting_down()) return E_ABORT;
        }
    }
    for (std::size_t index = 0; index < state.panes.size(); ++index) {
        if (changed_panes[index])
            state.panes[index].finish_layout(pane_layouts[index], dpi);
        if (index >= panedock::core::pane_count(group.layout_template))
            state.panes[index].laid_out_pane_rect().reset();
    }
    // A pass where nothing moved and the batch committed has repainted
    // everything it changed already; an unconditional whole-window invalidate
    // makes every no-op kDeferredLayoutMessage a full repaint (PD-207).
    // !committed means the batch fell back to per-window SetWindowPos, which
    // leaves the frame needing a repaint.
    if (!committed ||
        std::any_of(changed_panes.begin(), changed_panes.end(),
                    [](bool changed) noexcept { return changed; }))
        InvalidateRect(window, nullptr, FALSE);
    if (recompute_content) write_live_list_count(state.diagnostic_mode, state.panes);
    // `group` above outlived every Shell call in this pass; a changed group
    // count means one of them re-entered a Group mutation and the reference
    // was dangling.
    assert(state.application.groups.size() == group_count_on_entry);
    state.last_layout_failure = first_failure;
    return first_failure.has_value() ? first_failure->result : S_OK;
}

void refresh_startup_chrome(AppState& state) {
    ShellCallScope shell_call(state.reentry_guard);
    for (std::size_t index = 0; index < kPinnedFixedParsingNames.size();
         ++index) {
        if (state.is_shutting_down()) break;
        state.pinned_fixed_labels[index] = display_text_for_parsing_name(
            state, kPinnedFixedParsingNames[index]);
    }
    rebuild_pinned_location_menu(state);
    if (!state.is_shutting_down())
        refresh_tab_strips(state);
}

HRESULT realize_startup_panes(HWND window, AppState& state) {
    // startup_realize_pending keeps the first pass limited to the active pane.
    refresh_startup_chrome(state);
    if (state.is_shutting_down()) return E_ABORT;
    const HRESULT active_result = apply_layout(window, state);
    if (state.is_shutting_down()) return E_ABORT;
    if (has_active_group(state)) {
        const std::size_t active = active_pane_index(active_group(state));
        {
            ShellCallScope shell_call(state.reentry_guard);
            state.panes[active].focus_content();
        }
        if (state.is_shutting_down()) return E_ABORT;
    }

    state.startup_realize_pending = false;
    const HRESULT remaining_result = apply_layout(window, state, true);
    if (state.is_shutting_down()) return E_ABORT;
    if (FAILED(active_result)) return active_result;
    return remaining_result;
}

panedock::core::GroupState new_group_state(const AppState& state,
                                            std::string id) {
    auto group = default_application_state().groups.front();
    group.id = std::move(id);
    group.name = panedock::core::next_group_name(state.application);
    const auto target = has_active_group(state)
                            ? active_group(state).layout_template
                            : group.layout_template;
    if (target != group.layout_template) {
        panedock::core::switch_layout(group, target,
                                      default_shell_location());
    }
    for (std::size_t index = 0; index < group.panes.size(); ++index)
        active_tab(group.panes[index]).location = default_shell_location();
    return group;
}

// The ordered script every Group mutation runs once the model has changed.
// core::run_group_transition owns the order, which steps apply and the
// shutdown gate between them; this supplies the Win32 side of each step.
//
// Nothing here holds a GroupState& across a step. apply_layout and the Shell
// focus call both re-enter the message loop, and a reference into
// state.application.groups would not survive a Group being added or removed
// during that re-entry -- so every step looks the Group up again.
void show_startup_notification(HWND owner, AppState& state) noexcept;

void perform_group_transition(HWND window, AppState& state,
                              panedock::core::GroupTransition transition,
                              const std::string& previous_active_group_id) {
    panedock::app_shell::ScopedTiming timing(state.diagnostic_mode, "group_switch_ms");
    // The Win32 side of each step; core::run_group_transition owns the loop,
    // the per-step shutdown gate and the navigate-failure abort (PD-217).
    struct Effects final : panedock::core::GroupTransitionEffects {
        Effects(HWND window, AppState& state) noexcept
            : window_(window), state_(state) {}
        bool is_shutting_down() const noexcept override {
            return state_.is_shutting_down();
        }
        bool has_active_group() const noexcept override {
            return ::has_active_group(state_);
        }
        void rebind_panes() override { ::rebind_panes(state_); }
        void refresh_tab_strips() override { ::refresh_tab_strips(state_); }
        bool navigate_realized_panes() override {
            return ::navigate_realized_panes(state_, active_group(state_));
        }
        void apply_layout() override {
            const HRESULT hr = ::apply_layout(window_, state_);
            if (SUCCEEDED(hr)) return;
            report_shell_failure(state_, L"group_transition", hr);
            // E_ABORT is apply_layout's shutdown bail-out, not a failure the
            // user can act on. Anything else leaves a blank pane, which used
            // to be reported only to a debugger (PD-208); reuse the startup
            // path's modeless notification rather than adding a second
            // mechanism.
            if (hr == E_ABORT) return;
            append_startup_warning(
                state_,
                L"PaneDock could not open the list for one or more "
                L"panes. Some panes may be empty. " +
                    panedock::app_shell::format_shell_failure_detail(
                        hr, last_failed_pane(state_)));
            show_startup_notification(window_, state_);
        }
        void focus_active_pane() override {
            const std::size_t active = active_pane_index(active_group(state_));
            ShellCallScope shell_call(state_.reentry_guard);
            state_.panes[active].focus_content();
        }
        void refresh_sidebar() override { ::refresh_sidebar(state_); }
        void schedule_session_save() noexcept override {
            // Group switches can be triggered from the OLE drag-hover
            // message. Keep the session flush out of that interaction path;
            // shutdown still forces the dirty state through a synchronous
            // save.
            ::schedule_session_save(state_);
        }

    private:
        HWND window_;
        AppState& state_;
    } effects(window, state);
    panedock::core::run_group_transition(
        transition, previous_active_group_id,
        state.application.active_group_id, effects);
}

void activate_group(HWND window, AppState& state, std::size_t index) {
    if (state.is_shutting_down())
        return;  // re-dispatched during a Shell call or teardown pump
    if (index >= state.application.groups.size()) return;
    const std::string target_id = state.application.groups[index].id;
    if (target_id == state.application.active_group_id) {
        rebind_panes(state);
        refresh_sidebar(state);
        return;
    }

    const std::string previous = state.application.active_group_id;
    state.application.active_group_id = target_id;
    perform_group_transition(window, state,
                             panedock::core::GroupTransition::activate,
                             previous);
}

Microsoft::WRL::ComPtr<DragHoverTarget> make_sidebar_drag_hover_target(
    HWND window, AppState& state) {
    auto hit_test = [&state](POINT screen) -> std::optional<std::size_t> {
        const HWND list = state.sidebar.window();
        if (list == nullptr) return std::nullopt;
        POINT client = screen;
        ScreenToClient(list, &client);
        const LRESULT hit = SendMessageW(
            list, LB_ITEMFROMPOINT, 0, MAKELPARAM(client.x, client.y));
        const std::size_t index = static_cast<std::size_t>(LOWORD(hit));
        if (HIWORD(hit) != 0 || index >= state.application.groups.size())
            return std::nullopt;
        return index;
    };
    auto hover_callback = [&state, window](std::size_t index) {
        if (index < state.application.groups.size())
            activate_group(window, state, index);
    };
    return make_drag_hover_target(window, kDragHoverSidebarTimerId,
                                  std::move(hit_test),
                                  std::move(hover_callback),
                                  [&state](bool entering) noexcept {
                                      drag_state_changed(state, entering);
                                  });
}

void add_group(HWND window, AppState& state) {
    if (state.is_shutting_down()) return;
    const bool was_empty = state.application.groups.empty();
    const std::string id = panedock::core::next_group_id(state.application);
    if (!panedock::core::add_group(state.application,
                                   new_group_state(state, id))) return;
    if (was_empty) {
        // core made the first Group active as it added it, so there is no id
        // to switch to; run the same tail every other mutation runs.
        perform_group_transition(window, state,
                                 panedock::core::GroupTransition::activate,
                                 std::string{});
        return;
    }
    activate_group(window, state, state.application.groups.size() - 1);
}

std::optional<std::size_t> group_index_by_id(const AppState& state,
                                             const std::string& id) noexcept {
    const auto& groups = state.application.groups;
    for (std::size_t index = 0; index < groups.size(); ++index)
        if (groups[index].id == id) return index;
    return std::nullopt;
}

void duplicate_group(HWND window, AppState& state, const std::string& target) {
    if (state.is_shutting_down()) return;
    const auto selected = group_index_by_id(state, target);
    if (!selected.has_value()) return;
    const auto& source = state.application.groups[*selected];
    const std::string id = panedock::core::next_group_id(state.application);
    if (!panedock::core::duplicate_group(state.application, source.id, id,
                                         source.name + L" copy")) return;
    activate_group(window, state, state.application.groups.size() - 1);
}

void delete_group(HWND window, AppState& state, const std::string& target) {
    const auto selected = group_index_by_id(state, target);
    if (!selected.has_value()) return;
    // MessageBoxW pumps this thread's message loop with no Shell call in progress,
    // so every kDeferredCommandMessage still queued from a burst of clicks
    // replays *inside* the box -- including another add_group or delete_group.
    // The selected index therefore does not survive the box; the Group id
    // does. Resolve identity first and let core::delete_group report a Group
    // that is already gone.
    const std::string id = state.application.groups[*selected].id;
    if (MessageBoxW(window,
                    L"Delete this Group? This action cannot be undone.",
                    L"Delete Group", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    if (state.is_shutting_down()) return;

    // A command replayed inside the box may have deleted it already. Bail
    // before unbind(), which would otherwise leave every pane blank with no
    // transition to rebind it.
    const auto& groups = state.application.groups;
    if (std::none_of(groups.begin(), groups.end(),
                     [&](const auto& group) { return group.id == id; }))
        return;

    const std::string previous = state.application.active_group_id;
    // core::delete_group destroys only the deleted Group's PaneStates; PD-184
    // keeps every surviving PaneState address across the vector erase. The
    // panes borrow the active Group's PaneStates, so only deleting the active
    // Group must drop them first. Deleting an inactive Group leaves the live
    // lists bound: that transition does not re-navigate, so unbinding would
    // leave every visible pane empty.
    if (id == previous)
        for (auto& pane : state.panes) pane.unbind();
    if (!panedock::core::delete_group(state.application, id)) return;
    // next_group_id can hand the same id to a later Group; its snapshots must
    // never satisfy that Group's requests.
    Pane::discard_group_list_cache(id);
    perform_group_transition(window, state,
                             panedock::core::GroupTransition::remove,
                             previous);
}

// No rebind: PD-184 guarantees group reorder preserves PaneState addresses.
void move_group(AppState& state, const std::string& group_id, bool down) {
    const auto selected = group_index_by_id(state, group_id);
    if (!selected.has_value()) return;
    if ((!down && *selected == 0) ||
        (down && *selected + 1 >= state.application.groups.size())) return;
    const std::string id = state.application.groups[*selected].id;
    const std::size_t target = down ? *selected + 1 : *selected - 1;
    if (!panedock::core::reorder_group(state.application, id, target)) return;
    refresh_sidebar(state);
    schedule_session_save(state);
}

void set_active_pane(HWND window, AppState& state, std::size_t pane) noexcept {
    if (state.is_shutting_down()) return;
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    if (pane >= group.panes.size()) return;
    const std::size_t previous = active_pane_index(group);
    if (previous == pane ||
        !panedock::core::set_active_pane(group, group.panes[pane].id)) return;
    {
        ShellCallScope shell_call(state.reentry_guard);
        state.panes[pane].focus_content();
    }
    if (state.is_shutting_down()) return;
    state.panes[previous].set_chrome_active(false);
    state.panes[pane].set_chrome_active(true);
    InvalidateRect(window, nullptr, TRUE);
    schedule_session_save(state);
}


bool AppState::is_shutting_down() const noexcept {
    return shutdown_coordinator.is_shutting_down();
}

void AppState::schedule_session_save() noexcept {
    ::schedule_session_save(*this);
}

const std::string &AppState::active_group_id() const noexcept {
    static const std::string kEmptyGroupId;
    return has_active_group(*this) ? active_group(*this).id : kEmptyGroupId;
}

bool AppState::diagnostic_timing_enabled() const noexcept {
    return diagnostic_mode;
}

std::span<const panedock::app_shell::PinnedLocation>
AppState::pinned_locations() const noexcept {
    return pinned_location_menu_items;
}

void AppState::manage_pinned_locations() {
    if (pinned_location_menu_items.size() <
        panedock::app_shell::kPinnedMenuFixedLocationCount)
        return;
    show_pinned_locations_manager(main_window, *this);
}

void AppState::pin_location(panedock::core::ShellLocation location) {
    if (application.pinned_locations.size() >=
        static_cast<std::size_t>(kPinnedMenuMaxLocationCount))
        return;
    if (!panedock::core::add_pinned_location(application, location)) return;
    const auto &pinned = application.pinned_locations.back();
    AppState &state = *this;
    if (pinned_locations_dialog.is_open())
        pinned_locations_dialog.add_location(
            pinned, display_text_for_parsing_name(state, pinned.parsing_name));
    rebuild_pinned_location_menu(*this);
    ::schedule_session_save(*this);
}

std::wstring AppState::tab_display_text(std::wstring_view parsing_name) {
    AppState &state = *this;
    return ::tab_display_text(state, parsing_name);
}

HFONT AppState::chrome_font() const noexcept {
    return chrome_font_;
}

HWND AppState::tooltip() const noexcept {
    return layout_tooltip;
}

std::string AppState::make_unique_tab_id() const {
    std::size_t candidate = 0;
    return panedock::core::next_tab_id(active_group(*this), candidate);
}

std::optional<panedock::app_shell::TabStripDragLayout>
AppState::tab_drag_layout(const Pane &pane, HWND strip, int min_width,
                          int max_width, int text_reserve) const {
    const std::size_t pane_index = pane.index();
    const bool foreign_placeholder =
        tab_drag.has_value() && tab_drag->drag.dragging &&
        tab_drag->drag.target_pane == pane_index &&
        tab_drag->drag.source_pane != pane_index &&
        tab_drag->drag.target_index.has_value();
    if (foreign_placeholder) {
        int placeholder_width = min_width;
        if (tab_drag->drag.source_pane < panes.size() &&
            tab_drag->drag.source_index <
                panes[tab_drag->drag.source_pane].tab_strip_ui().tab_visuals().size()) {
            const auto &source = panes[tab_drag->drag.source_pane]
                                     .tab_strip_ui().tab_visuals()[tab_drag->drag.source_index];
            SIZE size{};
            HDC measure = GetDC(strip);
            const HGDIOBJ old = measure == nullptr
                                    ? nullptr
                                    : SelectObject(measure, chrome_font_);
            if (measure != nullptr) {
                GetTextExtentPoint32W(measure, source.text.c_str(),
                                      static_cast<int>(source.text.size()),
                                      &size);
                SelectObject(measure, old);
                ReleaseDC(strip, measure);
            }
            placeholder_width = std::clamp(
                static_cast<int>(size.cx) + text_reserve, min_width,
                max_width);
        }
        return panedock::app_shell::TabStripDragLayout{
            tab_drag->drag.source_index, tab_drag->drag.target_index, true,
            placeholder_width};
    }
    if (tab_drag.has_value() && tab_drag->drag.dragging &&
        tab_drag->drag.source_pane == pane_index &&
        !tab_drag->drag.target_pane.has_value() &&
        tab_drag->drag.target_index.has_value()) {
        return panedock::app_shell::TabStripDragLayout{
            tab_drag->drag.source_index, tab_drag->drag.target_index, false, 0};
    }
    return std::nullopt;
}

bool register_tab_drag_hover_targets(HWND window, AppState& state) {
    for (std::size_t pane_index = 0;
         pane_index < state.panes.size(); ++pane_index) {
        auto hit_test = [&state,
                         pane_index](POINT screen) -> std::optional<std::size_t> {
            if (state.panes[pane_index].pane_state() == nullptr)
                return std::nullopt;
            return state.panes[pane_index].tab_strip_ui().tab_at_screen(screen);
        };
        auto hover_callback = [&state, pane_index](std::size_t item) {
            state.panes[pane_index].activate_tab_at(item);
        };
        auto target = make_drag_hover_target(
            window, kDragHoverTabTimerIdBase + pane_index,
            std::move(hit_test), std::move(hover_callback),
            [&state](bool entering) noexcept {
                drag_state_changed(state, entering);
            });
        if (target == nullptr) return false;
        bool registered = false;
        {
            ShellCallScope shell_call(state.reentry_guard);
            registered =
                state.panes[pane_index]
                    .tab_strip_ui()
                    .register_drag_hover_target(target.Get(), target.Get());
        }
        if (state.is_shutting_down() || !registered)
            return false;
    }
    return true;
}

void set_layout(HWND window, AppState& state,
                panedock::core::LayoutTemplate target) noexcept {
    if (state.is_shutting_down()) return;
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    // Clicking the layout that is already selected changes nothing: no
    // splitter reset, no relayout, no session write.
    if (group.layout_template == target) return;

    std::vector<std::string> pane_ids;
    std::vector<std::string> tab_ids;
    const std::size_t target_count = panedock::core::pane_count(target);
    std::size_t tab_candidate = 0;
    for (std::size_t index = group.panes.size(); index < target_count; ++index) {
        pane_ids.push_back("pane-" + std::to_string(index));
        tab_ids.push_back(
            panedock::core::next_tab_id(group, tab_candidate));
    }
    if (!panedock::core::switch_layout(group, target,
                                       default_shell_location(),
                                       pane_ids, tab_ids)) return;
    // This is where the four copies had drifted: set_layout never refreshed
    // the sidebar, but the Group summary counts only the panes the current
    // layout shows, so the row went stale until the next unrelated refresh.
    perform_group_transition(window, state,
                             panedock::core::GroupTransition::relayout,
                             state.application.active_group_id);
}

void update_sidebar_drag(HWND window, AppState& state, POINT point,
                         bool recompute_content = false) {
    if (!state.sidebar_drag.has_value()) return;
    const int dpi = std::max(1, static_cast<int>(GetDpiForWindow(window)));
    const int delta = MulDiv(
        point.x - state.sidebar_drag->start_x, 96, dpi);
    state.application.sidebar_width = panedock::core::clamp_sidebar_width(
        state.sidebar_drag->start_width, delta);
    if (const HRESULT hr =
            apply_layout(window, state, false, recompute_content);
        FAILED(hr))
        report_shell_failure(state, L"sidebar_drag", hr);
}

void update_splitter_drag(HWND window, AppState& state, POINT point,
                          bool recompute_content = false) {
    if (!state.splitter_drag.has_value()) return;
    auto& group = active_group(state);
    const Splitter& drag = *state.splitter_drag;
    if (drag.ratio_index >= group.divider_ratios.size()) return;

    const RECT client = pane_content_area(window, state.application.sidebar_width);
    const auto ratio = panedock::core::divider_ratio_at(
        drag.vertical ? point.x - client.left : point.y - client.top,
        drag.vertical ? client.right - client.left
                      : client.bottom - client.top,
        layout_metrics(window).divider_thickness);
    if (!ratio.has_value()) return;
    group.divider_ratios[drag.ratio_index] = *ratio;
    if (const HRESULT hr =
            apply_layout(window, state, false, recompute_content);
        FAILED(hr))
        report_shell_failure(state, L"splitter_drag", hr);
}

std::size_t pane_at_point(HWND window, int stored_sidebar_width,
                          const panedock::core::GroupState& group,
                          POINT point) noexcept {
    const auto rects = layout_rects(window, stored_sidebar_width, group);
    for (std::size_t index = 0; index < rects.size(); ++index) {
        const RECT rect = to_win32_rect(rects[index]);
        if (PtInRect(&rect, point)) return index;
    }
    return kMaxPaneCount;
}

// The coordinator's guard: no active Group means no pane under any point.
std::size_t pane_at_point(HWND window, const AppState& state,
                          POINT point) noexcept {
    if (!has_active_group(state)) return kMaxPaneCount;
    return pane_at_point(window, state.application.sidebar_width,
                         active_group(state), point);
}

void capture_window_placement(HWND window, AppState& state) noexcept {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (!GetWindowPlacement(window, &placement)) {
        OutputDebugStringW(L"PaneDock: GetWindowPlacement failed\n");
        return;
    }
    // rcNormalPosition is in workspace coordinates (offset by a top/left
    // taskbar). Startup restores it through SetWindowPlacement, which reads
    // the same coordinate space, so the stored numbers keep their meaning.
    const RECT& normal = placement.rcNormalPosition;
    state.application.window_placement = {
        normal.left, normal.top, normal.right - normal.left,
        normal.bottom - normal.top,
        panedock::app_shell::placement_reopens_maximized(
            placement.showCmd == SW_SHOWMAXIMIZED,
            placement.showCmd == SW_SHOWMINIMIZED,
            (placement.flags & WPF_RESTORETOMAXIMIZED) != 0)};
}

const wchar_t* session_source_name(panedock::core::SessionSource source) {
    switch (source) {
        case panedock::core::SessionSource::primary: return L"primary";
        case panedock::core::SessionSource::interrupted_write:
            return L"interrupted_write";
        case panedock::core::SessionSource::backup: return L"backup";
        case panedock::core::SessionSource::default_state:
            return L"default_state";
    }
    return L"unknown";
}

POINT point_from_lparam(LPARAM lparam) noexcept {
    return {static_cast<short>(LOWORD(lparam)),
            static_cast<short>(HIWORD(lparam))};
}

bool address_bar_has_focus(
    const std::array<panedock::app_shell::Pane, kMaxPaneCount>& panes) noexcept {
    const HWND focused = GetFocus();
    for (const auto& chrome : panes)
        if (chrome.address_bar() == focused) return true;
    return false;
}

// Pane shortcuts (Backspace, Tab, Ctrl+V, Ctrl+W, ...) apply only to input
// aimed at the main window, and never while a text box outside the panes --
// the Group rename editor -- has focus. The panes' own edits (address bar,
// list rename) keep their routing through KeyInput::address_bar_focused.
bool pane_key_routing_applies(HWND window, const AppState& state,
                              const MSG& message) noexcept {
    if (message.hwnd == nullptr ||
        GetAncestor(message.hwnd, GA_ROOT) != window)
        return false;
    const HWND focused = GetFocus();
    if (focused == nullptr) return true;
    if (focused == state.sidebar.rename_editor()) return false;
    wchar_t class_name[16]{};
    if (GetClassNameW(focused, class_name, 16) == 0 ||
        _wcsicmp(class_name, L"Edit") != 0)
        return true;
    return std::any_of(state.panes.begin(), state.panes.end(),
                       [focused](const auto& pane) {
                           return pane.window() != nullptr &&
                                  IsChild(pane.window(), focused);
                       });
}

void cancel_tab_drag(AppState& state, HWND strip) noexcept {
    if (!state.tab_drag.has_value() || state.tab_drag->strip != strip)
        return;
    const std::size_t source = state.tab_drag->drag.source_pane;
    const auto target = state.tab_drag->drag.target_pane;
    state.tab_drag.reset();
    state.panes[source].tab_strip_ui().apply_item_size();
    if (target.has_value() && *target != source)
        state.panes[*target].tab_strip_ui().apply_item_size();
    if (GetCapture() == strip) ReleaseCapture();
}

void finish_tab_drag(AppState& state, HWND strip) {
    if (state.is_shutting_down()) return;
    if (!state.tab_drag.has_value() || state.tab_drag->strip != strip)
        return;
    AppState::TabDrag drag = std::move(*state.tab_drag);
    state.tab_drag.reset();
    state.panes[drag.drag.source_pane].tab_strip_ui().apply_item_size();
    if (drag.drag.target_pane.has_value())
        state.panes[*drag.drag.target_pane].tab_strip_ui().apply_item_size();
    if (GetCapture() == strip) ReleaseCapture();
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    const auto drop =
        panedock::core::resolve_tab_drop(drag.drag, group.panes.size());
    if (drop.effect == panedock::core::TabDragEffect::none) return;

    if (drop.effect == panedock::core::TabDragEffect::reorder) {
        if (!panedock::core::reorder_tab(
                *state.panes[drop.source_pane].pane_state(), drag.tab_id,
                drop.target_index))
            return;
        state.panes[drop.source_pane].tab_strip_ui().refresh();
        schedule_session_save(state);
        return;
    }

    auto& source = group.panes[drop.source_pane];
    auto& target = group.panes[drop.target_pane];
    const bool source_active = source.active_tab_id == drag.tab_id;
    // Reserved before the move: a single-tab source keeps a placeholder, and
    // it needs an id that is free across the whole group.
    const std::string retained_tab_id = state.make_unique_tab_id();
    if (!panedock::core::move_tab(
            source, target, drag.tab_id, drop.target_index,
            default_shell_location(), retained_tab_id)) return;
    if (source_active && state.panes[drop.source_pane].realized()) {
        {
            ShellCallScope shell_call(state.reentry_guard);
            state.panes[drop.source_pane].navigate_to(
                active_tab(source).location);
        }
        if (state.is_shutting_down()) return;
    }
    if (state.panes[drop.target_pane].realized()) {
        {
            ShellCallScope shell_call(state.reentry_guard);
            state.panes[drop.target_pane].navigate_to(
                active_tab(target).location);
        }
        if (state.is_shutting_down()) return;
    }
    state.panes[drop.source_pane].tab_strip_ui().refresh();
    state.panes[drop.target_pane].tab_strip_ui().refresh();
    schedule_session_save(state);
}

void update_tab_drag(AppState& state, HWND strip, WPARAM wparam,
                     LPARAM lparam) {
    if (!state.tab_drag.has_value() || state.tab_drag->strip != strip) return;
    if ((wparam & MK_LBUTTON) == 0) {
        cancel_tab_drag(state, strip);
        return;
    }
    const POINT point = point_from_lparam(lparam);
    const int threshold =
        std::max(1, std::max(GetSystemMetrics(SM_CXDRAG),
                             GetSystemMetrics(SM_CYDRAG)));
    if (!panedock::core::begin_tab_drag(state.tab_drag->drag, point.x,
                                        point.y, threshold))
        return;

    POINT screen = point;
    ClientToScreen(strip, &screen);

    // The only part of a drag that needs a window: which tab strip the cursor
    // is over and which slot within it. What that means is core's decision.
    panedock::core::TabStripHit hit;
    for (std::size_t index = 0; index < state.panes.size(); ++index) {
        auto* pane_state = state.panes[index].pane_state();
        if (pane_state == nullptr) continue;
        const HWND candidate = state.panes[index].tab_strip();
        if (!IsWindowVisible(candidate)) continue;
        POINT client_point = screen;
        ScreenToClient(candidate, &client_point);
        RECT client{};
        GetClientRect(candidate, &client);
        if (!PtInRect(&client, client_point)) continue;
        hit.pane = index;
        hit.slot = state.panes[index].tab_strip_ui().tab_at_screen(screen);
        hit.tab_count = pane_state->tabs.size();
        const RECT viewport = to_win32_rect(
            state.panes[index].tab_strip_ui().tab_geometry().viewport);
        hit.in_viewport = PtInRect(&viewport, client_point) != 0;
        break;
    }

    const auto previous_target = state.tab_drag->drag.target_pane;
    if (!panedock::core::update_tab_drag(state.tab_drag->drag, hit)) return;

    state.panes[state.tab_drag->drag.source_pane]
        .tab_strip_ui()
        .apply_item_size();
    if (previous_target.has_value() &&
        previous_target != state.tab_drag->drag.target_pane)
        state.panes[*previous_target].tab_strip_ui().apply_item_size();
    if (state.tab_drag->drag.target_pane.has_value())
        state.panes[*state.tab_drag->drag.target_pane]
            .tab_strip_ui()
            .apply_item_size();
}

panedock::app_shell::TabStripPaintState tab_strip_paint_state(
    const AppState &state, std::size_t pane_index) noexcept {
    panedock::app_shell::TabStripPaintState paint;
    if (state.panes[pane_index].pane_state() == nullptr) return paint;
    paint.active_pane = pane_index == active_pane_index(active_group(state));
    if (state.tab_drag.has_value() && state.tab_drag->drag.dragging) {
        const auto &drag = state.tab_drag->drag;
        if (drag.source_pane == pane_index)
            paint.dragged_index = drag.source_index;
        if (drag.target_pane.value_or(drag.source_pane) == pane_index &&
            state.panes[drag.source_pane].pane_state() != nullptr) {
            const auto &visuals =
                state.panes[drag.source_pane].tab_strip_ui().tab_visuals();
            if (drag.source_index < visuals.size())
                paint.placeholder_text = visuals[drag.source_index].text;
        }
    }
    return paint;
}

LRESULT CALLBACK tab_strip_proc(HWND window, UINT message, WPARAM wparam,
                                LPARAM lparam, UINT_PTR pane_index,
                                DWORD_PTR reference_data) {
    auto* state = reinterpret_cast<AppState*>(reference_data);
    if (state != nullptr && pane_index < state->panes.size()) {
        if (panedock::app_shell::child_message_blocked_while_closing(
                state->is_shutting_down(), message))
            return 0;
        if (defer_shell_reentry_mouse_message(window, *state, message,
                                              wparam, lparam))
            return 0;
        auto &strip = state->panes[pane_index].tab_strip_ui();
        if (message == WM_PAINT) {
            strip.paint(tab_strip_paint_state(*state, pane_index));
            return 0;
        }
        // Per-tab close button: press arms it (no tab drag, no activation),
        // release over the same button closes that tab by id through the
        // normal close path. A quick second click arrives as a double-click.
        if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) &&
            state->panes[pane_index].pane_state() != nullptr) {
            const auto close = strip.close_button_at(point_from_lparam(lparam));
            const auto& tabs = state->panes[pane_index].pane_state()->tabs;
            if (close.has_value() && *close < tabs.size()) {
                if (state->tab_drag.has_value())
                    cancel_tab_drag(*state, state->tab_drag->strip);
                strip.press_close(*close, tabs[*close].id);
                SetCapture(window);
                return 0;
            }
        }
        if (message == WM_LBUTTONUP && strip.pressed_close_index()) {
            const auto pressed = strip.pressed_close_index();
            const auto release = strip.close_button_at(point_from_lparam(lparam));
            const std::string tab_id = strip.take_pressed_close();
            if (GetCapture() == window) ReleaseCapture();
            if (release == pressed && !tab_id.empty())
                state->panes[pane_index].close_tab(tab_id);
            return 0;
        }
        if (message == WM_CAPTURECHANGED && strip.pressed_close_index())
            (void)strip.take_pressed_close();
        if (const auto handled = strip.handle_message(message, wparam, lparam))
            return *handled;
        if (message == WM_LBUTTONDOWN &&
            state->panes[pane_index].pane_state() != nullptr) {
            const POINT point = point_from_lparam(lparam);
            const auto item =
                state->panes[pane_index].tab_strip_ui().tab_at(point);
            const RECT add = to_win32_rect(
                state->panes[pane_index].tab_strip_ui().tab_geometry().add_rect);
            if (item.has_value()) {
                const auto& tabs =
                    state->panes[pane_index].pane_state()->tabs;
                if (*item >= tabs.size()) return 0;
                if (state->tab_drag.has_value())
                    cancel_tab_drag(*state, state->tab_drag->strip);
                panedock::core::TabDragState drag{};
                drag.source_pane = pane_index;
                drag.source_index = *item;
                drag.start_x = point.x;
                drag.start_y = point.y;
                state->tab_drag =
                    AppState::TabDrag{window, tabs[*item].id, drag};
                SetCapture(window);
                // Only the drag bookkeeping above spans panes; selecting the
                // tab is the pane's own business.
                state->panes[pane_index].activate_tab_at(*item);
            } else if (PtInRect(&add, point)) {
                state->panes[pane_index].add_default_tab();
            }
            return 0;
        }
        if (message == WM_MOUSEMOVE) {
            strip.mouse_move(point_from_lparam(lparam));
            update_tab_drag(*state, window, wparam, lparam);
            return 0;
        }
        if (message == WM_LBUTTONUP) {
            finish_tab_drag(*state, window);
            return 0;
        }
        if (message == WM_CAPTURECHANGED) {
            cancel_tab_drag(*state, window);
            return 0;
        }
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(window, tab_strip_proc, pane_index);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT CALLBACK hover_tracking_proc(HWND window, UINT message, WPARAM wparam,
                                     LPARAM lparam, UINT_PTR button_id,
                                     DWORD_PTR reference_data) {
    auto* state = reinterpret_cast<AppState*>(reference_data);
    if (state != nullptr) {
        if (message == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            if (state->owner_draw_hovered_button != window) {
                const HWND previous = state->owner_draw_hovered_button;
                state->owner_draw_hovered_button = window;
                if (previous != nullptr)
                    InvalidateRect(previous, nullptr, FALSE);
                InvalidateRect(window, nullptr, FALSE);
            }
        } else if (message == WM_MOUSELEAVE) {
            if (state->owner_draw_hovered_button == window) {
                state->owner_draw_hovered_button = nullptr;
                InvalidateRect(window, nullptr, FALSE);
            }
        } else if (message == WM_RBUTTONUP) {
            const auto control = panedock::app_shell::decode_pane_control(
                static_cast<int>(button_id));
            if (control != panedock::app_shell::PaneControl::folder_context)
                return DefSubclassProc(window, message, wparam, lparam);
            // Transform right-click activation into the existing BN_CLICKED
            // route so it opens the same native folder context menu.
            const HWND parent = GetParent(window);
            if (parent != nullptr)
                SendMessageW(
                    parent, WM_COMMAND,
                    MAKEWPARAM(static_cast<WORD>(button_id), BN_CLICKED),
                    reinterpret_cast<LPARAM>(window));
            return 0;
        } else if (message == WM_NCDESTROY) {
            if (state->owner_draw_hovered_button == window)
                state->owner_draw_hovered_button = nullptr;
            RemoveWindowSubclass(window, hover_tracking_proc, button_id);
        }
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

// The list's own mouse behaviour lives in Sidebar. This keeps only what is
// coordinator work: the shutdown and Shell-reentry gates, applying a finished
// reorder to the model, and the subclass teardown.
LRESULT CALLBACK group_list_proc(HWND window, UINT message, WPARAM wparam,
                                 LPARAM lparam, UINT_PTR,
                                 DWORD_PTR reference_data) {
    auto* state = reinterpret_cast<AppState*>(reference_data);
    if (state == nullptr)
        return DefSubclassProc(window, message, wparam, lparam);
    if (panedock::app_shell::child_message_blocked_while_closing(
            state->is_shutting_down(), message))
        return 0;
    if (defer_shell_reentry_mouse_message(window, *state, message, wparam,
                                          lparam))
        return 0;
    if (message == WM_NCDESTROY) {
        state->sidebar.cancel_drag();
        RemoveWindowSubclass(window, group_list_proc, 0);
        return DefSubclassProc(window, message, wparam, lparam);
    }

    const auto handled =
        state->sidebar.handle_list_message(window, message, wparam, lparam);
    if (const auto reorder = state->sidebar.take_reorder_request();
        reorder.has_value() &&
        panedock::core::reorder_group(state->application, reorder->group_id,
                                      reorder->target_index)) {
        refresh_sidebar(*state);
        schedule_session_save(*state);
    }
    if (handled.has_value()) return *handled;
    return DefSubclassProc(window, message, wparam, lparam);
}

void drag_state_changed(AppState& state, bool entering) noexcept {
    state.shutdown_coordinator.drag_changed(entering);
}

void set_main_window_title(HWND window, bool diagnostic_mode,
                           bool closing) noexcept {
    if (window == nullptr) return;
    const wchar_t* const title =
        closing ? L"PaneDock \x2014 Closing..."
                : (diagnostic_mode ? L"PaneDock \x2014 Diagnostic Mode"
                                   : L"PaneDock");
    SetWindowTextW(window, title);
    RedrawWindow(window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
}

void show_startup_notification(HWND owner, AppState& state) noexcept {
    if (owner == nullptr || state.startup_warning_message.empty() ||
        state.closing_ || state.quit_requested)
        return;
    state.startup_notification.show(owner, state.chrome_font_);
}

void AppShutdownEffects::show_closing_caption(bool closing) noexcept {
    set_main_window_title(state_.main_window, state_.diagnostic_mode, closing);
}

bool AppShutdownEffects::has_window() const noexcept {
    return state_.main_window != nullptr;
}

bool AppShutdownEffects::window_alive() const noexcept {
    return IsWindow(state_.main_window) != FALSE;
}

bool AppShutdownEffects::post_deferred_shutdown() noexcept {
    if (PostMessageW(state_.main_window, kDeferredShutdownMessage, 0, 0))
        return true;
    OutputDebugStringW(L"PaneDock: could not queue deferred shutdown\n");
    return false;
}

void AppShutdownEffects::show_transfer_prompt() noexcept {
    state_.transfer_close_dialog.show(state_.main_window);
}

void AppShutdownEffects::dismiss_transfer_prompt() noexcept {
    state_.transfer_close_dialog.destroy();
}

bool AppShutdownEffects::save_session() noexcept {
    capture_window_placement(state_.main_window, state_);
    return save_now(state_);
}

bool AppShutdownEffects::ask_keep_open_after_save_failure() noexcept {
    const int answer = MessageBoxW(
        state_.main_window,
        L"PaneDock could not save your session. Keep PaneDock open so "
        L"you can fix the storage problem and try again? Choose No to "
        L"close without saving recent changes.",
        L"PaneDock", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1);
    return answer != IDNO;
}

// ShutdownCoordinator runs this after teardown_started and before the parent
// window goes: everything that could still reach a Shell view, then the views.
void AppShutdownEffects::destroy_views(bool session_ending) noexcept {
    auto& state = state_;
    const HWND window = state.main_window;
    // Keep the owner visible while revoking OLE and releasing list resources.
    // Pane destroys list children before their parent HWNDs.
    // The caption is a progress surface for a user who is watching. At an OS
    // session end nobody is, and RedrawWindow(RDW_UPDATENOW | RDW_FRAME)
    // forces a synchronous non-client paint while DWM is itself shutting
    // down -- cost on the one path that has a kill timeout, for a string no
    // one reads.
    if (!session_ending) set_main_window_title(window, state.diagnostic_mode, true);
    state.startup_realize_pending = false;
    ++state.startup_realize_generation;
    state.transfer_close_dialog.destroy();
    state.startup_notification.destroy();
    state.pinned_locations_dialog.destroy();
    revoke_drag_hover_targets(state);
    cancel_session_save_timer(state);
    destroy_panes(state);
    // The leak invariant must be checkable in the shipped build: assert() is
    // compiled out by -DNDEBUG in Release, which is the configuration the
    // launch smoke test runs. Emit the count so the test can assert on it.
    write_live_list_count(state.diagnostic_mode, state.panes);
    assert(std::none_of(state.panes.begin(), state.panes.end(),
        [](const auto& pane) { return pane.realized(); }));
}

void AppShutdownEffects::destroy_window() noexcept {
    DestroyWindow(state_.main_window);
    PostQuitMessage(0);
}

// The teardown tail: COM, then the durable clean-shutdown marker. Both exits
// run it -- wWinMain's, and run_end_session_shutdown, which never returns to
// the message loop. The ordering is the whole point: keep the marker false
// until Shell, the parent HWND and COM have all gone away, so a teardown that
// dies partway through still reports an incomplete shutdown. If the write
// blocks or fails the false marker simply remains.
void finalize_process(AppState& state) noexcept {
    if (state.ole_finalized) return;
    state.ole_finalized = true;
    OleUninitialize();
    if (state.shutdown_clean_marker_armed && state.main_window_destroyed)
        (void)state.session.write_clean_marker(state.application);
}

// WM_ENDSESSION cannot defer. Windows terminates the process as soon as the
// handler returns, so a posted kDeferredShutdownMessage is never pumped: the
// session save, the ExplorerBrowser teardown and the clean-shutdown marker
// would every one of them be skipped on an OS restart, and the next startup
// reports an unclean shutdown. Drive the states the posted continuation would
// have driven, synchronously, while the handler still owns the thread.
void run_end_session_shutdown(AppState& state) noexcept {
    state.shutdown_coordinator.end_session_confirmed();
    if (state.main_window_destroyed) finalize_process(state);
}

// The durable checkpoint, deliberately reaching no pane. Traced on a
// real reboot: the teardown after it reached the second pane and never
// returned from its IExplorerBrowser::Destroy, and the process was killed
// there -- while the identical messages driven by SendMessage complete in
// 59ms. At a genuine session end Explorer is being torn down concurrently, so
// Destroy and the per-pane Shell reads it then made are cross-process calls
// into a dying server and can outlast the kill timeout. This write reaches no
// Shell and no COM -- it serializes the model and replaces two local files,
// and the KillTimer/SetTimer it does on the way out are local to our own
// thread. Storage can still be slow, so this is the cheapest step available,
// not a guaranteed-fast one.
//
// This overrides the "marker only after Shell, the HWND and COM are gone"
// ordering, on this path only. That ordering exists to report a teardown that
// crashed, and it is unreachable inside the time Windows grants; an
// OS-initiated session end is not the crash the marker warns about. Read the
// marker here as "a restorable checkpoint exists", not "teardown finished".
// What the skipped live read would have refreshed is each pane's current
// location and any view state not yet mirrored into the model -- the last
// few hundred milliseconds of a session that is ending.
bool AppShutdownEffects::write_end_session_checkpoint() noexcept {
    capture_window_placement(state_.main_window, state_);
    return state_.session.write(state_.application, true, state_.main_window);
}

void handle_transfer_close_dialog_result(AppState& state) noexcept {
    const auto result = state.transfer_close_dialog.take_result();
    if (!result.has_value()) return;
    switch (*result) {
        case panedock::app_shell::TransferCloseDialog::Result::keep_open:
            state.shutdown_coordinator.transfer_chosen(
                panedock::core::TransferChoice::keep_open);
            return;
        case panedock::app_shell::TransferCloseDialog::Result::
            close_after_transfer:
            state.shutdown_coordinator.transfer_chosen(
                panedock::core::TransferChoice::close_after_transfer);
            return;
        case panedock::app_shell::TransferCloseDialog::Result::
            cancel_and_close:
            state.shutdown_coordinator.transfer_chosen(
                panedock::core::TransferChoice::cancel_and_close);
            return;
    }
}

void file_operation_started(void* context) noexcept {
    if (context != nullptr)
        static_cast<AppState*>(context)->shutdown_coordinator.sequence().step(
            panedock::core::ShutdownEvent::file_operation_started);
}

void file_operation_finished(void* context) noexcept {
    if (context == nullptr) return;
    auto& state = *static_cast<AppState*>(context);
    state.shutdown_coordinator.sequence().step(
        panedock::core::ShutdownEvent::file_operation_finished);
    if (state.main_window != nullptr)
        PostMessageW(state.main_window, kFileOperationFinishedMessage, 0, 0);
}

bool file_operation_cancel_requested(void* context) noexcept {
    if (context == nullptr) return false;
    const auto& state = *static_cast<AppState*>(context);
    return state.cancel_file_operation || state.is_shutting_down();
}

struct FileCommandContext final {
    AppState& state;
    Pane& pane;
    Pane::FileCommandSnapshot snapshot;
    HWND foreground{};
    bool current() const noexcept {
        return !state.is_shutting_down() && has_active_group(state) &&
            active_pane_index(active_group(state)) == pane.index() &&
            GetForegroundWindow() == foreground && pane.file_command_is_current(snapshot);
    }
};

bool file_command_setup_aborted(void* context) noexcept {
    return !static_cast<FileCommandContext*>(context)->current();
}
void file_command_started(void* context) noexcept {
    file_operation_started(&static_cast<FileCommandContext*>(context)->state);
}
void file_command_finished(void* context) noexcept {
    file_operation_finished(&static_cast<FileCommandContext*>(context)->state);
}
bool file_command_cancel_requested(void* context) noexcept {
    return file_operation_cancel_requested(&static_cast<FileCommandContext*>(context)->state);
}

// The one bracket around every Pane file operation: the shutdown sequence
// sees the call start and finish, the Shell call pumps inside a
// ShellCallScope, and a close deferred meanwhile completes right after.
// The caller has already gated on file_operation_call_active and shutdown.
template <class Operation>
auto run_file_operation(AppState& state, FileCommandContext& context,
                        Operation operation) {
    state.shutdown_coordinator.sequence().step(
        panedock::core::ShutdownEvent::file_operation_call_started);
    decltype(operation(std::declval<const panedock::file_operations::Callbacks&>())) result;
    {
        ShellCallScope shell_call(state.reentry_guard);
        const panedock::file_operations::Callbacks callbacks{
            &context, file_command_started, file_command_finished,
            file_command_cancel_requested, file_command_setup_aborted};
        result = operation(callbacks);
    }
    state.shutdown_coordinator.sequence().step(
        panedock::core::ShutdownEvent::file_operation_finished);
    state.shutdown_coordinator.sequence().step(
        panedock::core::ShutdownEvent::file_operation_call_finished);
    state.shutdown_coordinator.complete_deferred_close();
    return result;
}

// Shared gate for the Pane-driven single-item Shell operations (rename, new folder).
template <class Operation>
panedock::file_operations::RenameResult run_pane_item_operation(AppState& state,
    Pane& pane, const Pane::FileCommandSnapshot& snapshot, Operation operation) noexcept try {
    panedock::file_operations::RenameResult result;
    result.result = E_ABORT;
    if (state.file_operation_call_active || state.is_shutting_down()) return result;
    FileCommandContext context{state, pane, snapshot, GetForegroundWindow()};
    if (context.foreground != state.main_window || !context.current()) return result;
    return run_file_operation(state, context, operation);
} catch (...) {
    panedock::file_operations::RenameResult result;
    result.result = E_OUTOFMEMORY;
    return result;
}

panedock::file_operations::RenameResult AppState::new_folder(
    Pane& pane, const Pane::FileCommandSnapshot& snapshot) noexcept {
    return run_pane_item_operation(*this, pane, snapshot, [&](const auto& callbacks) {
        return panedock::file_operations::new_folder(
            main_window, snapshot.folder.parsing_name, callbacks);
    });
}

panedock::file_operations::RenameResult AppState::rename_item(
    Pane& pane, const Pane::FileCommandSnapshot& snapshot, std::wstring_view name) noexcept {
    if (snapshot.items.size() != 1) {
        panedock::file_operations::RenameResult result;
        result.result = E_ABORT;
        return result;
    }
    return run_pane_item_operation(*this, pane, snapshot, [&](const auto& callbacks) {
        return panedock::file_operations::rename_item(
            main_window, snapshot.items.front(), name, callbacks);
    });
}

// A failed paste or delete is never silent; a user cancel or abort says nothing.
const wchar_t* file_command_failure_text(panedock::file_operations::Command command,
    const panedock::file_operations::PasteResult& result) noexcept {
    using panedock::file_operations::Command;
    if (SUCCEEDED(result.result) || result.aborted || result.result == E_ABORT ||
        result.result == HRESULT_FROM_WIN32(ERROR_CANCELLED) ||
        result.result == COPYENGINE_E_USER_CANCELLED) return nullptr;
    if (command == Command::paste)
        return result.result == DV_E_FORMATETC
            ? L"Nothing on the clipboard can be pasted here."
            : L"Paste failed.";
    if (command == Command::delete_items || command == Command::delete_permanently)
        return L"Delete failed.";
    return L"Copy to the clipboard failed.";
}

panedock::file_operations::Command file_command_for(
    panedock::core::KeyAction action) noexcept {
    using panedock::file_operations::Command;
    switch (action) {
    case panedock::core::KeyAction::cut: return Command::cut;
    case panedock::core::KeyAction::delete_items: return Command::delete_items;
    case panedock::core::KeyAction::delete_permanently:
        return Command::delete_permanently;
    case panedock::core::KeyAction::paste: return Command::paste;
    default: return Command::copy;
    }
}

bool perform_file_command(HWND window, AppState& state, Pane& pane,
                          panedock::file_operations::Command command) noexcept try {
    if (state.file_operation_call_active || state.is_shutting_down() ||
        state.reentry_guard.in_shell_call()) return true;
    FileCommandContext context{state, pane, {}, GetForegroundWindow()};
    if (context.foreground != window) return true;
    const HRESULT captured = pane.file_command_snapshot(command, context.snapshot);
    if (captured != S_OK) return command != panedock::file_operations::Command::paste;
    if (!context.current()) return true;
    const auto result = run_file_operation(state, context, [&](const auto& callbacks) {
        using panedock::file_operations::Command;
        if (command == Command::paste)
            return panedock::file_operations::paste_from_clipboard(
                window, context.snapshot.folder.parsing_name, callbacks);
        if (command == Command::delete_items || command == Command::delete_permanently)
            return panedock::file_operations::delete_selection(
                window, context.snapshot.items, callbacks,
                command == Command::delete_items);
        panedock::file_operations::PasteResult copied;
        copied.result = panedock::file_operations::selection_to_clipboard(
            context.snapshot.items, command == Command::cut, callbacks);
        copied.handled = true;
        return copied;
    });
    if (command == panedock::file_operations::Command::cut && SUCCEEDED(result.result) &&
        !state.is_shutting_down())
        pane.note_list_cut(context.snapshot); // Explorer dims cut rows; re-verifies itself
    if (!state.is_shutting_down() && context.current()) {
        if (const wchar_t* text = file_command_failure_text(command, result))
            pane.show_status_text(text);
    }
    return state.is_shutting_down() || !context.current() || result.handled;
} catch (...) { return true; }

bool activate_main_window_on_own_thread(HWND window) noexcept;

LRESULT create_main_window_children(HWND window, AppState& state) {
    state.startup_realize_pending = true;
    state.startup_frame_only = true;
    // Every fatal child-construction failure (sidebar, subclass, control
    // create, drag-drop registration) returns -1 below, which makes
    // CreateWindowExW return null and wWinMain show this message. Without
    // this default those paths would exit with no user-facing prompt.
    state.startup_error_message =
        L"PaneDock could not create its user interface.";
    INITCOMMONCONTROLSEX controls{
        sizeof(controls), ICC_TAB_CLASSES | ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&controls)) return -1;
    state.sidebar_drag_target = make_sidebar_drag_hover_target(window, state);
    bool sidebar_created = false;
    {
        ShellCallScope shell_call(state.reentry_guard);
        sidebar_created = state.sidebar.create(
            window, kGroupListId, state.sidebar_drag_target.Get());
    }
    if (state.is_shutting_down()) return 0;
    if (!sidebar_created) {
        state.sidebar_drag_target.Reset();
        return -1;
    }
    if (!SetWindowSubclass(state.sidebar.window(), group_list_proc, 0,
                           reinterpret_cast<DWORD_PTR>(&state))) {
        state.sidebar.revoke_drag_drop();
        state.sidebar_drag_target.Reset();
        return -1;
    }
    state.group_label = CreateWindowExW(
        0, L"STATIC", L"GROUPS",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    if (state.group_label == nullptr) return -1;
    for (std::size_t index = 0; index < state.sidebar_buttons.size(); ++index) {
        state.sidebar_buttons[index] = CreateWindowExW(
            0, L"BUTTON", kButtonLabels[index],
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(kButtonIds[index]),
            GetModuleHandleW(nullptr), nullptr);
        if (state.sidebar_buttons[index] == nullptr) return -1;
        if (!SetWindowSubclass(state.sidebar_buttons[index],
                               hover_tracking_proc,
                               static_cast<UINT_PTR>(kButtonIds[index]),
                               reinterpret_cast<DWORD_PTR>(&state)))
            return -1;
    }
    for (std::size_t index = 0; index < state.layout_buttons.size(); ++index) {
        state.layout_buttons[index] = CreateWindowExW(
            0, L"BUTTON", kLayoutButtonLabels[index],
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON |
                BS_OWNERDRAW | (index == 0 ? WS_GROUP : 0),
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(kLayoutButtonIds[index]),
            GetModuleHandleW(nullptr), nullptr);
        if (state.layout_buttons[index] == nullptr) return -1;
        if (!SetWindowSubclass(state.layout_buttons[index], hover_tracking_proc,
                               index,
                               reinterpret_cast<DWORD_PTR>(&state)))
            return -1;
    }
    state.layout_tooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    if (state.layout_tooltip != nullptr) {
        constexpr std::array<const wchar_t*, 8> kLayoutTooltips{
            L"Single pane", L"Two panes side by side", L"Two panes stacked",
            L"Three panes", L"Two panes on the left, one on the right",
            L"One pane over two panes", L"Two panes over one pane",
            L"Four panes"};
        for (std::size_t index = 0; index < state.layout_buttons.size();
             ++index) {
            add_tooltip(state.layout_tooltip, window,
                        reinterpret_cast<UINT_PTR>(
                            state.layout_buttons[index]),
                        kLayoutTooltips[index]);
        }
    }
    state.empty_message = CreateWindowExW(
        0, L"STATIC", L"No Group. Click New Group to get started.",
        WS_CHILD | SS_CENTER | SS_CENTERIMAGE, 0, 0, 0, 0, window, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (state.empty_message == nullptr) return -1;
    for (std::size_t index = 0; index < state.panes.size(); ++index) {
        auto& chrome = state.panes[index];
        // PaneDock.Pane owns this pane's chrome. Its explorer container stays
        // a plain STATIC used only for clipping and list parenting.
        chrome.set_host(&state);
        chrome.set_reentry_guard(&state.reentry_guard);
        if (!chrome.create(window, static_cast<int>(index))) return -1;
        if (!SetWindowSubclass(chrome.tab_strip(), tab_strip_proc, index,
                               reinterpret_cast<DWORD_PTR>(&state)))
            return -1;
        const std::array<HWND, 6> buttons{
            chrome.back_button(), chrome.forward_button(), chrome.up_button(),
            chrome.refresh_button(), chrome.view_mode_button(),
            chrome.pinned_button()};
        constexpr std::array pane_controls{
            panedock::app_shell::PaneControl::back,
            panedock::app_shell::PaneControl::forward,
            panedock::app_shell::PaneControl::up,
            panedock::app_shell::PaneControl::refresh,
            panedock::app_shell::PaneControl::view_mode,
            panedock::app_shell::PaneControl::pinned};
        for (std::size_t button = 0; button < buttons.size(); ++button) {
            const int id =
                panedock::app_shell::encode_pane_control(pane_controls[button]);
            if (!SetWindowSubclass(buttons[button], hover_tracking_proc,
                                   static_cast<UINT_PTR>(id),
                                   reinterpret_cast<DWORD_PTR>(&state)))
                return -1;
        }
        if (state.layout_tooltip != nullptr) {
            constexpr std::array<const wchar_t*, 6> tooltips{
                L"Back", L"Forward", L"Up", L"Refresh", L"View",
                L"Pinned locations"};
            for (std::size_t button = 0; button < buttons.size(); ++button) {
                add_tooltip(state.layout_tooltip, window,
                            reinterpret_cast<UINT_PTR>(buttons[button]),
                            tooltips[button]);
            }
        }
        if (!SetWindowSubclass(
                chrome.folder_context_button(), hover_tracking_proc,
                static_cast<UINT_PTR>(
                    panedock::app_shell::encode_pane_control(
                        panedock::app_shell::PaneControl::folder_context)),
                reinterpret_cast<DWORD_PTR>(&state)))
            return -1;
        EnableWindow(chrome.folder_context_button(), FALSE);
        if (state.layout_tooltip != nullptr) {
            add_tooltip(state.layout_tooltip, window,
                        reinterpret_cast<UINT_PTR>(
                            chrome.folder_context_button()),
                        L"Folder context menu");
        }
    }
    rebind_panes(state);
    refresh_ui_font(window, state);
    refresh_sidebar(state);
    if (const HRESULT hr = apply_layout(window, state); FAILED(hr)) {
        // A single unreachable pane (offline drive, permission or AV block,
        // invalid stored location) must not prevent the app from opening. Keep
        // the window; the failing pane stays unrealized (blank) and the user is
        // told after create.
        report_shell_failure(state, L"create", hr);
        append_startup_warning(
            state,
            L"PaneDock could not open the list for one or more panes. "
            L"Some panes may be empty. " +
                panedock::app_shell::format_shell_failure_detail(
                    hr, last_failed_pane(state)));
    }
    if (state.is_shutting_down()) return 0;
    if (!register_tab_drag_hover_targets(window, state)) {
        OutputDebugStringW(
            L"PaneDock: RegisterDragDrop for tab strip failed\n");
        revoke_drag_hover_targets(state);
        append_startup_warning(
            state, L"PaneDock could not enable tab drag-and-drop.");
    }
    if (state.is_shutting_down()) return 0;
    return 0;
}

bool handle_sidebar_command(HWND window, AppState& state, int id) {
    using panedock::app_shell::CommandKind;
    using panedock::app_shell::GroupAction;
    const auto command = panedock::app_shell::decode_command(id);
    if (command.kind == CommandKind::group_list) {
        const auto selected = state.sidebar.selected_index();
        if (selected.has_value()) activate_group(window, state, *selected);
        return true;
    }
    if (command.kind != CommandKind::group_action) return false;
    // A context-menu command acts on the right-clicked Group; any other
    // source acts on the active Group. Resolved by id, never by row index.
    std::string target = state.group_menu_target.has_value()
                             ? std::exchange(*state.group_menu_target, {})
                             : state.application.active_group_id;
    state.group_menu_target.reset();
    switch (command.action) {
        case GroupAction::create: add_group(window, state); return true;
        case GroupAction::duplicate:
            duplicate_group(window, state, target);
            return true;
        case GroupAction::rename:
            if (const auto index = group_index_by_id(state, target))
                state.sidebar.begin_rename(*index);
            return true;
        case GroupAction::remove: delete_group(window, state, target); return true;
        case GroupAction::move_up: move_group(state, target, false); return true;
        case GroupAction::move_down: move_group(state, target, true); return true;
    }
    return false;
}

bool handle_global_command(HWND window, AppState& state, int id,
                           Pane* source_pane = nullptr) {
    using panedock::app_shell::CommandKind;
    const auto command = panedock::app_shell::decode_command(id);
    if (command.kind == CommandKind::pane_control &&
        command.control == panedock::app_shell::PaneControl::folder_context) {
        if (source_pane == nullptr) return false;
        auto& pane = *source_pane;
        const HWND folder_context_button =
            pane.folder_context_button();
        if (pane.pane_state() == nullptr ||
            !pane.realized() ||
            folder_context_button == nullptr ||
            !IsWindowVisible(folder_context_button))
            return true;
        set_active_pane(state.main_window, state, pane.index());
        if (state.is_shutting_down()) return true;
        if (active_pane_index(active_group(state)) != pane.index()) return true;
        RECT button_rect{};
        if (!GetWindowRect(folder_context_button, &button_rect))
            return true;
        const POINT anchor{button_rect.left, button_rect.top};
        ShellCallScope shell_call(state.reentry_guard);
        (void)pane.show_folder_context_menu(
            state.main_window, anchor);
        return true;
    }
    if (command.kind == CommandKind::layout_template) {
        set_layout(window, state, kLayoutTemplates[command.index]);
        return true;
    }
    return false;
}

bool draw_global_control(const DRAWITEMSTRUCT& item, AppState& state) {
    const auto command =
        panedock::app_shell::decode_command(static_cast<int>(item.CtlID));
    if (item.CtlType == ODT_BUTTON &&
        command.kind == panedock::app_shell::CommandKind::layout_template) {
        const std::size_t index = command.index;
        const bool enabled = has_active_group(state);
        const auto current = enabled ? active_group(state).layout_template
                                     : panedock::core::LayoutTemplate::single;
        draw_layout_button(item, index, kLayoutTemplates[index] == current,
                           state.owner_draw_hovered_button == item.hwndItem);
        return true;
    }
    if (item.CtlType == ODT_BUTTON) {
        const auto found = std::find(kButtonIds.begin(), kButtonIds.end(),
                                     static_cast<int>(item.CtlID));
        if (found != kButtonIds.end()) {
            const auto label_index =
                static_cast<std::size_t>(found - kButtonIds.begin());
            draw_sidebar_action_button(
                item, kButtonLabels[label_index],
                state.owner_draw_hovered_button == item.hwndItem);
            return true;
        }
    }
    return state.sidebar.draw_item(&item);
}

bool handle_context_menu(HWND target, AppState& state, POINT screen) {
    const HWND window = state.main_window;
    if (target != state.sidebar.window()) return false;

    POINT point = screen;
    if (screen.x == -1 && screen.y == -1) {
        // Keyboard-invoked (Shift+F10 / menu key): anchor near the currently
        // selected row instead of a stale cursor position.
        RECT rect{};
        GetWindowRect(target, &rect);
        point = {rect.left + scaled_value(window, 12),
                 rect.top + scaled_value(window, 12)};
    }
    POINT client_point = point;
    ScreenToClient(target, &client_point);
    const LRESULT hit = SendMessageW(
        target, LB_ITEMFROMPOINT, 0,
        MAKELPARAM(client_point.x, client_point.y));
    const std::size_t index = static_cast<std::size_t>(LOWORD(hit));
    if (HIWORD(hit) != 0 || index >= state.application.groups.size())
        return true;

    // The menu targets the right-clicked row by id. The selection is left
    // alone: it mirrors the active Group, and refresh_sidebar restores it.
    const std::string target_id = state.application.groups[index].id;
    state.group_menu_target.reset();

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return true;
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kRenameGroupId),
                L"Rename Group");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kDuplicateGroupId),
                L"Duplicate Group");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kDeleteGroupId),
                L"Delete Group");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (index == 0 ? MF_GRAYED : 0),
                static_cast<UINT_PTR>(kMoveUpId), L"Move Up");
    AppendMenuW(menu,
                MF_STRING |
                    (index + 1 >= state.application.groups.size() ? MF_GRAYED
                                                                  : 0),
                static_cast<UINT_PTR>(kMoveDownId), L"Move Down");
    const std::array<std::pair<UINT, wchar_t>, 5> icons{{
        {kRenameGroupId, L'\uE70F'},
        {kDuplicateGroupId, L'\uE8C8'},
        {kDeleteGroupId, L'\uE74D'},
        {kMoveUpId, L'\uE74A'},
        {kMoveDownId, L'\uE74B'},
    }};
    std::array<HBITMAP, icons.size()> bitmaps{};
    for (std::size_t i = 0; i < icons.size(); ++i) {
        const auto [id, glyph] = icons[i];
        const bool disabled = (GetMenuState(menu, id, MF_BYCOMMAND) &
                               (MF_DISABLED | MF_GRAYED)) != 0;
        bitmaps[i] = panedock::app_shell::create_menu_icon(
            glyph, GetDpiForWindow(window),
            GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_MENUTEXT));
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_BITMAP;
        item.hbmpItem = bitmaps[i];
        SetMenuItemInfoW(menu, id, FALSE, &item);
    }
    SetForegroundWindow(window);
    // Without the menu's Shell depth a deferred sidebar click could replay
    // inside it and move the selection, and Delete would hit another Group.
    const int command =
        panedock::app_shell::track_popup_menu(state.reentry_guard, menu, point, window);
    DestroyMenu(menu);
    for (const auto bitmap : bitmaps) {
        if (bitmap != nullptr) DeleteObject(bitmap);
    }
    if (state.is_shutting_down()) return true;
    if (command != 0) {
        // The menu pumped; the Group may be gone. Each handler re-resolves
        // the id and ignores a Group that no longer exists. Sent rather than
        // run here: this menu can open during an outer Shell call, and the
        // window proc defers WM_COMMAND until that call returns.
        state.group_menu_target = target_id;
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(command, 0), 0);
    }
    return true;
}

std::optional<LRESULT> handle_global_mouse_message(
    HWND window, AppState& state, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_LBUTTONDOWN: {
            const POINT point = point_from_lparam(lparam);
            if (sidebar_boundary_at_point(window, state.application.sidebar_width,
                                         point)) {
                state.sidebar_drag = AppState::SidebarDrag{
                    point.x,
                    panedock::core::clamp_sidebar_width(
                        state.application.sidebar_width, 0)};
                SetCapture(window);
                return 0;
            }
            if (has_active_group(state)) {
                state.splitter_drag =
                    splitter_at_point(window, state.application.sidebar_width,
                                      active_group(state), point);
                if (state.splitter_drag.has_value()) {
                    SetCapture(window);
                    return 0;
                }
            }
            break;
        }
        case WM_MOUSEMOVE:
            if ((state.sidebar_drag.has_value() ||
                 state.splitter_drag.has_value()) &&
                (wparam & MK_LBUTTON) != 0) {
                const POINT point = point_from_lparam(lparam);
                if (state.sidebar_drag.has_value())
                    update_sidebar_drag(window, state, point, false);
                if (state.splitter_drag.has_value())
                    update_splitter_drag(window, state, point, false);
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (state.sidebar_drag.has_value()) {
                update_sidebar_drag(window, state, point_from_lparam(lparam),
                                    true);
                state.sidebar_drag.reset();
                schedule_session_save(state);
                ReleaseCapture();
                return 0;
            }
            if (state.splitter_drag.has_value()) {
                update_splitter_drag(window, state, point_from_lparam(lparam),
                                     true);
                state.splitter_drag.reset();
                schedule_session_save(state);
                ReleaseCapture();
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            state.sidebar_drag.reset();
            state.splitter_drag.reset();
            return 0;
        case WM_LBUTTONDBLCLK:
            if (has_active_group(state)) {
                const auto splitter = splitter_at_point(
                    window, state.application.sidebar_width,
                    active_group(state), point_from_lparam(lparam));
                if (splitter.has_value()) {
                    active_group(state)
                        .divider_ratios[splitter->ratio_index] = 0.5;
                    apply_layout(window, state);
                    schedule_session_save(state);
                    return 0;
                }
            }
            break;
        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                if (state.sidebar_drag.has_value() ||
                    sidebar_boundary_at_point(window, state.application.sidebar_width,
                                         point)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
                if (has_active_group(state)) {
                    const auto splitter = splitter_at_point(
                        window, state.application.sidebar_width,
                        active_group(state), point);
                    if (splitter.has_value()) {
                        SetCursor(LoadCursorW(
                            nullptr,
                            splitter->vertical ? IDC_SIZEWE : IDC_SIZENS));
                        return TRUE;
                    }
                }
            }
            break;
        case WM_XBUTTONDOWN:
        case WM_PARENTNOTIFY:
            if (message == WM_XBUTTONDOWN ||
                LOWORD(wparam) == WM_XBUTTONDOWN) {
                const WORD button = message == WM_XBUTTONDOWN
                                        ? GET_XBUTTON_WPARAM(wparam)
                                        : HIWORD(wparam);
                if (button == XBUTTON1 || button == XBUTTON2) {
                    POINT point{};
                    GetCursorPos(&point);
                    ScreenToClient(window, &point);
                    const std::size_t pane = pane_at_point(window, state, point);
                    if (pane < kMaxPaneCount)
                        state.panes[pane].navigate_history(button == XBUTTON1);
                    return message == WM_XBUTTONDOWN ? std::optional<LRESULT>(TRUE)
                                                     : std::optional<LRESULT>(0);
                }
            }
            if (has_active_group(state) && LOWORD(wparam) == WM_MBUTTONDOWN) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                // Which pane the click landed in is coordinator work; the
                // tab under it is the pane's own.
                if (const std::size_t pane = pane_at_point(window, state, point);
                    pane < state.panes.size()) {
                    POINT screen = point;
                    ClientToScreen(window, &screen);
                    state.panes[pane].close_tab_at_screen(screen);
                }
                return 0;
            }
            if (LOWORD(wparam) == WM_LBUTTONDOWN) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                const std::size_t pane = pane_at_point(window, state, point);
                if (pane < kMaxPaneCount) set_active_pane(window, state, pane);
            }
            return 0;
        default: break;
    }
    return std::nullopt;
}

// WM_ACTIVATE / WM_SETFOCUS: give the list its focus back, or remember to
// once the Shell call pumping the loop returns. No Group, no list.
void restore_active_list_focus(HWND window, AppState& state) noexcept {
    Pane* pane = active_pane(state);
    if (pane == nullptr) return;
    if (state.reentry_guard.in_shell_call()) {
        pane->note_list_focus_activation();
        state.reentry_guard.hold(
            window, kDeferredListFocusRestoreMessage, 0, 0, true);
    } else {
        pane->restore_list_focus_after_activation();
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam,
                             LPARAM lparam) {
    auto* state = reinterpret_cast<AppState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = panedock::app_shell::window_state_from_create<AppState>(
            window, lparam);
        if (state == nullptr) return FALSE;
        state->main_window = window;
        state->session.set_timer_owner(window);
    }

    const bool close_request =
        message == WM_CLOSE ||
        (message == WM_SYSCOMMAND && (wparam & 0xfff0u) == SC_CLOSE) ||
        ((message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONUP ||
          message == WM_NCLBUTTONDBLCLK) && wparam == HTCLOSE);
    // Keep caption/frame repaint messages flowing while shutdown gates normal
    // work. set_main_window_title sends WM_SETTEXT; WM_NCPAINT, WM_PAINT and
    // WM_ERASEBKGND must then run so "Closing..." can be shown before the
    // synchronous Shell teardown starts.
    if (state != nullptr && (state->is_shutting_down()) &&
        !close_request && message != WM_QUERYENDSESSION &&
        message != WM_ENDSESSION && message != WM_DESTROY &&
        message != WM_NCDESTROY && message != WM_PAINT &&
        message != WM_ERASEBKGND && message != WM_SETTEXT &&
        message != WM_NCPAINT && message != kDeferredShutdownMessage)
        return 0;

    if (state != nullptr && state->reentry_guard.in_shell_call()) {
        if (message == WM_COMMAND) {
            defer_shell_reentry_message(window, *state,
                                        kDeferredCommandMessage, wparam,
                                        lparam);
            return 0;
        }
        if (message == kDragHoverMessage) {
            defer_shell_reentry_message(window, *state, message, wparam, lparam);
            return 0;
        }
        if (message == WM_PARENTNOTIFY || message == WM_LBUTTONDOWN ||
            message == WM_LBUTTONDBLCLK || message == WM_LBUTTONUP) {
            defer_shell_reentry_message(window, *state, message, wparam, lparam);
            return 0;
        }
    }

    switch (message) {
        case WM_CREATE: return create_main_window_children(window, *state);
        case kDeferredShutdownMessage:
            if (state != nullptr)
                state->shutdown_coordinator.deferred_shutdown_ready();
            return 0;
        case kDeferredCommandMessage:
            if (state != nullptr) {
                if (state->reentry_guard.in_shell_call()) {
                    defer_shell_reentry_message(
                        window, *state, kDeferredCommandMessage, wparam,
                        lparam);
                } else if (!state->is_shutting_down()) {
                    SendMessageW(window, WM_COMMAND, wparam, lparam);
                }
            }
            return 0;
        case kDeferredListFocusRestoreMessage:
            if (state != nullptr && !state->is_shutting_down()) {
                if (state->reentry_guard.in_shell_call()) {
                    state->reentry_guard.hold(
                        window, kDeferredListFocusRestoreMessage, 0, 0, true);
                } else if (Pane* pane = active_pane(*state)) {
                    pane->restore_list_focus_after_activation();
                }
            }
            return 0;
        case WM_SETTINGCHANGE:
            // SHGetSetSettings broadcasts "ShellState" when Explorer's folder
            // options change. Post once; the refresh runs outside this
            // broadcast and outside any Shell call that is pumping the loop.
            if (state != nullptr && !state->is_shutting_down() && lparam != 0 &&
                !state->shell_settings_refresh_queued &&
                _wcsicmp(reinterpret_cast<LPCWSTR>(lparam), L"ShellState") == 0 &&
                PostMessageW(window, kDeferredShellSettingsMessage, 0, 0))
                state->shell_settings_refresh_queued = true;
            break;
        case kDeferredShellSettingsMessage:
            if (state == nullptr) return 0;
            state->shell_settings_refresh_queued = false;
            if (state->reentry_guard.in_shell_call()) {
                state->reentry_guard.hold(
                    window, kDeferredShellSettingsMessage, 0, 0, true);
                return 0;
            }
            // Visible panes re-enumerate through the Refresh path, which keeps
            // the snapshot and reconciles names and hidden rows by identity.
            // Inactive tabs and cached Group snapshots rescan on activation.
            for (std::size_t index = 0; index < state->panes.size(); ++index) {
                if (state->is_shutting_down() || !has_active_group(*state)) return 0;
                if (index >= panedock::core::pane_count(
                                 active_group(*state).layout_template))
                    break;
                state->panes[index].refresh_view();
            }
            return 0;
        case kDeferredLayoutMessage:
            if (state != nullptr && state->layout_message_queued) {
                state->layout_message_queued = false;
                state->layout_pending = false;
                if (!state->is_shutting_down())
                    (void)apply_layout(window, *state, false, false);
            }
            return 0;
        case kDeferredRealizeMessage: {
            if (state == nullptr || !state->startup_realize_pending ||
                static_cast<UINT_PTR>(lparam) !=
                    state->startup_realize_generation)
                return 0;
            const HRESULT hr = realize_startup_panes(window, *state);
            if (state->is_shutting_down()) return 0;
            if (FAILED(hr)) {
                // A pane whose list could not be realized stays blank;
                // update the non-blocking startup notification instead of
                // entering a modal loop after the app is already usable.
                report_shell_failure(*state, L"deferred_realize", hr);
                append_startup_warning(
                    *state,
                    L"PaneDock could not open the list for one or more "
                    L"panes. Some panes may be empty. " +
                        panedock::app_shell::format_shell_failure_detail(
                            hr, last_failed_pane(*state)));
                show_startup_notification(window, *state);
            }
            return 0;
        }
        case kDragHoverMessage: {
            if (state == nullptr) return 0;
            const UINT_PTR timer = static_cast<UINT_PTR>(wparam);
            const UINT_PTR generation = static_cast<UINT_PTR>(lparam);
            if (timer == kDragHoverSidebarTimerId &&
                state->sidebar_drag_target != nullptr) {
                state->sidebar_drag_target->invoke_hover(generation);
                return 0;
            }
            if (timer >= kDragHoverTabTimerIdBase &&
                timer < kDragHoverTabTimerIdBase + kMaxPaneCount) {
                const std::size_t pane_index = static_cast<std::size_t>(
                    timer - kDragHoverTabTimerIdBase);
                if (auto* hover =
                        state->panes[pane_index].tab_strip_ui().drag_hover_timer())
                    hover->invoke_hover(generation);
                return 0;
            }
            return 0;
        }
        case kActivateExistingInstanceMessage:
            if (state == nullptr || state->closing_ || state->quit_requested)
                return 1;
            return activate_main_window_on_own_thread(window) ? 0 : 1;
        case WM_MEASUREITEM:
            if (state != nullptr) {
                auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
                if (item != nullptr && item->CtlType == ODT_BUTTON &&
                    panedock::app_shell::decode_command(
                        static_cast<int>(item->CtlID))
                            .kind ==
                        panedock::app_shell::CommandKind::layout_template) {
                    item->itemWidth = static_cast<UINT>(
                        scaled_value(window, kLayoutButtonWidth));
                    item->itemHeight = static_cast<UINT>(
                        scaled_value(window, kLayoutButtonHeight));
                    return TRUE;
                }
                if (state->sidebar.measure_item(item, GetDpiForWindow(window)))
                    return TRUE;
            }
            break;
        case WM_DRAWITEM: {
            if (state == nullptr) break;
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
            if (item == nullptr) break;
            if (draw_global_control(*item, *state)) return TRUE;
            break;
        }
        case WM_CTLCOLORSTATIC:
            if (state != nullptr) {
                const HWND control = reinterpret_cast<HWND>(lparam);
                if (control == state->group_label) {
                    const HDC dc = reinterpret_cast<HDC>(wparam);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, RGB(152, 162, 179));
                    SetTextCharacterExtra(dc, scaled_value(window, 1));
                    return reinterpret_cast<LRESULT>(
                        GetSysColorBrush(COLOR_WINDOW));
                }
            }
            break;
        case WM_PAINT:
            if (state != nullptr) {
                PAINTSTRUCT paint{};
                const HDC dc = BeginPaint(window, &paint);
                if (dc != nullptr) {
                    const int saved = SaveDC(dc);
                    IntersectClipRect(dc, paint.rcPaint.left,
                                      paint.rcPaint.top, paint.rcPaint.right,
                                      paint.rcPaint.bottom);
                    paint_client_background(
                        window, dc, current_sidebar_width(window, state->application.sidebar_width),
                        state->layout_buttons);
                    RestoreDC(dc, saved);
                    EndPaint(window, &paint);
                    return 0;
                }
            }
            break;
        case WM_PRINTCLIENT:
            if (state != nullptr && (lparam & PRF_CLIENT) != 0) {
                paint_client_background(
                    window, reinterpret_cast<HDC>(wparam),
                    current_sidebar_width(window, state->application.sidebar_width),
                    state->layout_buttons);
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            if (state != nullptr) return 1;
            break;
        case WM_CONTEXTMENU: {
            if (state == nullptr) break;
            const HWND target = reinterpret_cast<HWND>(wparam);
            const POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (handle_context_menu(target, *state, screen)) return 0;
            break;
        }
        case WM_COMMAND: {
            if (state == nullptr) break;
            const int id = LOWORD(wparam);
            if (id == kGroupListId && HIWORD(wparam) == LBN_SELCHANGE) {
                (void)handle_sidebar_command(window, *state, id);
                return 0;
            }
            if (HIWORD(wparam) != BN_CLICKED) break;
            if (handle_global_command(window, *state, id)) return 0;
            if (handle_sidebar_command(window, *state, id)) return 0;
            break;
        }
        case panedock::sidebar::kRenameCommitMessage:
            if (state != nullptr) {
                auto rename = state->sidebar.take_rename();
                if (rename.has_value()) {
                    // An empty or whitespace-only name is rejected and the
                    // old name stays, as in Explorer.
                    std::wstring name =
                        panedock::core::normalize_group_name(rename->name);
                    if (!name.empty() &&
                        panedock::core::rename_group(state->application,
                                                     rename->group_id,
                                                     std::move(name))) {
                        refresh_sidebar(*state);
                        schedule_session_save(*state);
                    }
                }
            }
            return 0;
        case WM_SIZE:
            if (state != nullptr) {
                if (const HRESULT hr =
                        apply_layout(window, *state, false, false);
                    FAILED(hr))
                    report_shell_failure(*state, L"WM_SIZE", hr);
            }
            return 0;
        case WM_ACTIVATE:
            if (state != nullptr && LOWORD(wparam) != WA_INACTIVE &&
                HIWORD(wparam) == 0 && !state->is_shutting_down())
                restore_active_list_focus(window, *state);
            break;
        case WM_SETFOCUS:
            if (state != nullptr && !state->is_shutting_down())
                restore_active_list_focus(window, *state);
            break;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            Pane::release_navigation_icon_font();
            release_brand_resources();
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (state != nullptr) {
                for (auto& pane : state->panes)
                    pane.laid_out_pane_rect().reset();
            }
            if (state != nullptr) refresh_ui_font(window, *state);
            if (state != nullptr) {
                if (const HRESULT hr = apply_layout(window, *state);
                    FAILED(hr))
                    report_shell_failure(*state, L"WM_DPICHANGED", hr);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
        case WM_LBUTTONDBLCLK:
        case WM_SETCURSOR:
        case WM_XBUTTONDOWN:
        case WM_PARENTNOTIFY:
            if (state != nullptr) {
                const auto result = handle_global_mouse_message(
                    window, *state, message, wparam, lparam);
                if (result.has_value()) return *result;
            }
            if (message == WM_PARENTNOTIFY) return 0;
            break;
        case WM_TIMER: {
            if (state == nullptr) break;
            const UINT_PTR timer = static_cast<UINT_PTR>(wparam);
            if (timer == kSessionSaveTimerId) {
                // WM_TIMER is not on the Shell re-entry deferral list, so
                // this can fire inside a Shell call that is pumping the loop.
                // save_now captures live pane locations, which mid-navigation
                // reads the outgoing folder. Leave the timer armed instead of
                // deferring by hand: it refires after the call unwinds, and a
                // 500ms retry cannot become a PostMessage spin.
                if (state->reentry_guard.in_shell_call()) return 0;
                state->session.cancel_timer(window);
                if (state->session.dirty()) (void)save_now(*state);

                return 0;
            }
            if (timer == kDragHoverSidebarTimerId &&
                state->sidebar_drag_target != nullptr) {
                state->sidebar_drag_target->timer_expired();
                return 0;
            }
            if (timer >= kDragHoverTabTimerIdBase &&
                timer < kDragHoverTabTimerIdBase + kMaxPaneCount) {
                const std::size_t pane_index = static_cast<std::size_t>(
                    timer - kDragHoverTabTimerIdBase);
                if (auto* hover =
                        state->panes[pane_index].tab_strip_ui().drag_hover_timer())
                    hover->timer_expired();
                return 0;
            }
            break;
        }
        case kFileOperationFinishedMessage:
            if (state != nullptr) {
                state->shutdown_coordinator.complete_deferred_close();
                if (!state->is_shutting_down())
                    PostMessageW(window, kDeferredFileOperationRefreshMessage, 0, 0);
            }
            return 0;
        case WM_DEVICECHANGE:
            // Volume arrival/removal reaches top-level windows unregistered.
            // The lparam struct dies with this send: post the unit mask.
            if (state != nullptr && !state->is_shutting_down() && lparam != 0 &&
                (wparam == DBT_DEVICEARRIVAL || wparam == DBT_DEVICEREMOVECOMPLETE) &&
                reinterpret_cast<const DEV_BROADCAST_HDR*>(lparam)->dbch_devicetype == DBT_DEVTYP_VOLUME)
                PostMessageW(window, kDeferredVolumeChangeMessage,
                    reinterpret_cast<const DEV_BROADCAST_VOLUME*>(lparam)->dbcv_unitmask,
                    wparam == DBT_DEVICEREMOVECOMPLETE);
            break;
        case kDeferredFileOperationRefreshMessage:
        case kDeferredVolumeChangeMessage:
            if (state == nullptr || state->is_shutting_down() || !has_active_group(*state)) return 0;
            if (state->reentry_guard.in_shell_call()) {
                state->reentry_guard.hold(window, message, wparam, lparam,
                    message == kDeferredFileOperationRefreshMessage);
                return 0;
            }
            for (std::size_t index = 0; index < state->panes.size(); ++index) {
                if (state->is_shutting_down() || !has_active_group(*state) ||
                    index >= panedock::core::pane_count(active_group(*state).layout_template))
                    break;
                if (message == kDeferredFileOperationRefreshMessage)
                    state->panes[index].refresh_unwatched_list();
                else
                    state->panes[index].volume_changed(static_cast<DWORD>(wparam), lparam != 0);
            }
            return 0;
        case panedock::app_shell::kTransferCloseDialogResultMessage:
            if (state != nullptr)
                handle_transfer_close_dialog_result(*state);
            return 0;
        case WM_CLOSE:
            // A pending confirmed session end re-enters as end_session.
            if (state != nullptr) state->shutdown_coordinator.request_close();
            return 0;
        case WM_DESTROY:
            if (state != nullptr) {
                (void)state->shutdown_coordinator.sequence().step(
                    panedock::core::ShutdownEvent::window_destroyed);
                state->startup_realize_pending = false;
                ++state->startup_realize_generation;
                state->pinned_locations_dialog.destroy();
                revoke_drag_hover_targets(*state);
                cancel_session_save_timer(*state);
                if (state->session.dirty() &&
                    !state->shutdown_save_attempted) {

                    state->shutdown_coordinator.sequence().step(
                        panedock::core::ShutdownEvent::save_started);
                    capture_window_placement(window, *state);
                    (void)save_now(*state);
                }
            }
            Pane::release_navigation_icon_font();
            release_brand_resources();
            Pane::release_address_bar_background_brush();
            return 0;
        case WM_NCDESTROY:
            if (state != nullptr) {
                revoke_drag_hover_targets(*state);
                release_ui_font(*state);
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            }
            break;
        case WM_QUERYENDSESSION:
            // Query only asks whether shutdown may proceed. Saving a clean
            // marker here makes a later WM_ENDSESSION(FALSE) look clean and can
            // block the system query on slow storage. The confirmed path below
            // performs the normal close sequence.
            return TRUE;
        case WM_ENDSESSION:
            if (state != nullptr) {
                if (wparam) {
                    run_end_session_shutdown(*state);
                } else {
                    state->shutdown_coordinator.end_session_cancelled();
                }
            }
            return 0;
        default: break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool register_window_class(HINSTANCE instance) noexcept {
    if (!panedock::app_shell::Pane::register_window_class(instance))
        return false;
    if (!panedock::app_shell::PinnedLocationsDialog::register_window_class(
            instance))
        return false;
    if (!panedock::app_shell::TransferCloseDialog::register_window_class(
            instance))
        return false;
    if (!panedock::app_shell::StartupNotification::register_window_class(
            instance))
        return false;
    return panedock::app_shell::register_simple_window_class(
        kWindowClassName, window_proc, instance,
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1), CS_DBLCLKS,
        LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON)),
        LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON)));
}

bool activate_main_window_on_own_thread(HWND window) noexcept {
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);

    const DWORD current_thread = GetCurrentThreadId();
    const HWND foreground_window = GetForegroundWindow();
    const DWORD foreground_thread =
        foreground_window == nullptr
            ? 0
            : GetWindowThreadProcessId(foreground_window, nullptr);
    const bool attached =
        foreground_thread != 0 && foreground_thread != current_thread &&
        AttachThreadInput(current_thread, foreground_thread, TRUE) != FALSE;
    const BOOL activated = SetForegroundWindow(window);
    if (attached) {
        (void)AttachThreadInput(current_thread, foreground_thread, FALSE);
    }
    if (activated == FALSE)
        OutputDebugStringW(L"PaneDock: existing window activation failed\n");
    return activated != FALSE;
}

// A previous instance holds the single-instance mutex. Either activate its
// main window (running), wait for it to release the mutex and start fresh
// (finishing a close), or report that it is alive but unresponsive.
// Returns true if wWinMain must exit now; on false the caller keeps `mutex`
// (a freshly acquired handle) and proceeds to launch a new instance.
bool relay_or_wait_for_existing_instance(HANDLE& mutex) noexcept {
    CloseHandle(mutex);
    // The main window is the honest signal of a usable instance, but a closing
    // instance destroys its window early and keeps the mutex until teardown
    // ends. Poll both: activate the window if it appears, launch fresh the
    // moment the previous releases the mutex, otherwise tell the user rather
    // than exiting with no visible result.
    const ULONGLONG deadline =
        GetTickCount64() + kSingleInstanceWindowRetryTimeoutMs;
    for (;;) {
        const HWND window = FindWindowW(kWindowClassName, nullptr);
        if (window != nullptr) {
            DWORD process_id = 0;
            if (GetWindowThreadProcessId(window, &process_id) != 0 &&
                process_id != 0)
                (void)AllowSetForegroundWindow(process_id);
            DWORD_PTR activation_result = 0;
            const LRESULT delivered = SendMessageTimeoutW(
                window, kActivateExistingInstanceMessage, 0, 0,
                SMTO_ABORTIFHUNG | SMTO_BLOCK,
                kSingleInstanceActivationTimeoutMs, &activation_result);
            if (delivered != 0 && activation_result == 0) return true;
        }
        const HANDLE probe = OpenMutexW(SYNCHRONIZE, FALSE,
                                        kSingleInstanceMutexName);
        if (probe == nullptr) {
            const DWORD probe_error = GetLastError();
            if (probe_error != ERROR_FILE_NOT_FOUND) {
                MessageBoxW(
                    nullptr,
                    L"PaneDock could not check whether another instance is "
                    L"running.",
                    L"PaneDock", MB_OK | MB_ICONERROR);
                return true;
            }
            // The previous instance released the mutex while we waited, so it is
            // gone. Take a fresh handle and let the caller launch normally.
            mutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutexName);
            if (mutex == nullptr) {
                OutputDebugStringW(L"PaneDock: CreateMutexW failed\n");
                MessageBoxW(
                    nullptr,
                    L"PaneDock could not acquire its single-instance lock.",
                    L"PaneDock", MB_OK | MB_ICONERROR);
                return true;
            }
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                // Another waiter won the close/relaunch race. Keep waiting for
                // its window instead of launching a second session writer.
                CloseHandle(mutex);
                mutex = nullptr;
                continue;
            }
            return false;
        }
        CloseHandle(probe);
        if (GetTickCount64() >= deadline) break;
        Sleep(kSingleInstanceWindowRetryIntervalMs);
    }
    MessageBoxW(
        nullptr,
        L"PaneDock is already running but not responding, or it is "
        L"shutting down. Wait a moment and try again.",
        L"PaneDock", MB_OK | MB_ICONWARNING);
    return true;
}

void report_startup_failure(const wchar_t* message) noexcept {
    MessageBoxW(nullptr, message, L"PaneDock", MB_OK | MB_ICONERROR);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    HANDLE single_instance_mutex =
        CreateMutexW(nullptr, FALSE, kSingleInstanceMutexName);
    if (single_instance_mutex == nullptr) {
        OutputDebugStringW(L"PaneDock: CreateMutexW failed\n");
        report_startup_failure(
            L"PaneDock could not start (the single-instance lock was "
            L"unavailable).");
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (relay_or_wait_for_existing_instance(single_instance_mutex)) return 0;
        // The previous instance released the mutex during the wait; fall through
        // and launch a fresh window with the acquired handle.
    }

    bool diagnostic_mode = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr) {
        const bool requested = panedock::app_shell::diagnostic_requested(
            argc, argv);
        LocalFree(argv);
        if (requested) {
            PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY policy{};
            policy.MicrosoftSignedOnly = 1;
            diagnostic_mode = SetProcessMitigationPolicy(
                                  ProcessSignaturePolicy, &policy,
                                  sizeof(policy)) != 0;
            if (diagnostic_mode) {
                // PD-070: MicrosoftSignedOnly makes the loader reject every
                // non-Microsoft-signed shell extension with
                // STATUS_INVALID_IMAGE_HASH, and by default the loader shows
                // a modal image-error box for each one. That box blocks the
                // unattended crash-attribution run this mode exists for, and
                // its text blames the extension's vendor for a block we
                // asked for. Suppress the box; the load still fails, just
                // silently. Only in diagnostic mode: in normal mode the box
                // is the user's only clue that an extension is genuinely
                // broken.
                SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS);
            } else {
                OutputDebugStringW(
                    L"PaneDock: diagnostic mode requested but "
                    L"SetProcessMitigationPolicy failed\n");
            }
        }
    }

    const HRESULT com_result = OleInitialize(nullptr);
    if (FAILED(com_result)) {
        report_startup_failure(L"PaneDock could not initialize COM.");
        CloseHandle(single_instance_mutex);
        return static_cast<int>(com_result);
    }

    int exit_code = 1;
    if (!SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        OutputDebugStringW(L"PaneDock: SetProcessDpiAwarenessContext failed\n");
    if (!register_window_class(instance)) {
        report_startup_failure(
            L"PaneDock could not create its main window class.");
        OleUninitialize();
        CloseHandle(single_instance_mutex);
        return exit_code;
    }

    AppState state;
    state.diagnostic_mode = diagnostic_mode;
    std::optional<std::filesystem::path> directory;
    {
        ShellCallScope shell_call(state.reentry_guard);
        directory = panedock::shell_core::session_directory();
    }
    if (state.is_shutting_down()) {
        OleUninitialize();
        CloseHandle(single_instance_mutex);
        return exit_code;
    }
    if (!directory.has_value()) {
        report_startup_failure(
            L"PaneDock could not resolve its data folder.");
        OleUninitialize();
        CloseHandle(single_instance_mutex);
        return exit_code;
    }
    state.session.set_directory(*directory);
    state.session.bind(state.application);
    auto loaded = panedock::core::read_session(
        state.session.directory(), default_application_state());

    const bool recovered_from_corruption = loaded.recovered_from_corruption;
    const auto session_source = loaded.source;
    const bool clean_shutdown = loaded.document.clean_shutdown;
    if (recovered_from_corruption) {
        OutputDebugStringW(L"PaneDock: session recovery source=");
        OutputDebugStringW(session_source_name(session_source));
        OutputDebugStringW(L"\n");
    }
    state.application = loaded.document.application;
    state.session.adopt(std::move(loaded.document));

    assert(panedock::core::is_valid(state.application));
    if (!save_now(state)) {
        append_startup_warning(
            state,
            L"PaneDock could not save its session. Changes may not persist "
            L"until the storage problem is fixed.");
    }

    // PD-134: validate the restored placement against the virtual desktop.
    // window_placement is saved from GetWindowPlacement's rcNormalPosition,
    // which can reference a monitor that no longer exists (external display
    // unplugged, dock removed). Restoring that verbatim leaves the window
    // created but fully off-screen -- "opened" with no visible UI. CW_USEDEFAULT
    // lets Windows pick an on-screen cascade slot.
    auto placement = state.application.window_placement;
    const int virtual_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtual_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int virtual_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int virtual_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (virtual_width > 0 && virtual_height > 0 &&
        placement.x != CW_USEDEFAULT &&
        panedock::app_shell::placement_is_offscreen(
            placement.x, placement.y, placement.width, placement.height,
            virtual_x, virtual_y, virtual_width, virtual_height)) {
        placement.x = CW_USEDEFAULT;
        placement.y = CW_USEDEFAULT;
        if (placement.width <= 0) placement.width = 1000;
        if (placement.height <= 0) placement.height = 700;
    }
    const wchar_t* const title = diagnostic_mode
                                     ? L"PaneDock \x2014 Diagnostic Mode"
                                     : L"PaneDock";
    HWND window = CreateWindowExW(
        0, kWindowClassName, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        placement.x,
        placement.y, placement.width, placement.height, nullptr, nullptr,
        instance, &state);
    if (window == nullptr) {
        destroy_panes(state);
        write_live_list_count(state.diagnostic_mode, state.panes);
        assert(std::none_of(state.panes.begin(), state.panes.end(),
        [](const auto& pane) { return pane.realized(); }));
        std::wstring fatal_message = state.startup_error_message;
        if (!state.startup_warning_message.empty()) {
            if (!fatal_message.empty()) fatal_message += L"\n\n";
            fatal_message += state.startup_warning_message;
        }
        if (!fatal_message.empty())
            MessageBoxW(nullptr, fatal_message.c_str(), L"PaneDock",
                        MB_ICONERROR | MB_OK);
        OleUninitialize();
        CloseHandle(single_instance_mutex);
        return exit_code;
    }
    // The saved rect is rcNormalPosition, in workspace coordinates, while
    // CreateWindowExW takes screen coordinates: with a top or left taskbar
    // the two differ and the window would drift on every restart. Apply it
    // through SetWindowPlacement, which also gives a maximized window the
    // right restore rect. SW_HIDE keeps the first show below.
    if (placement.x != CW_USEDEFAULT && placement.y != CW_USEDEFAULT &&
        placement.width > 0 && placement.height > 0) {
        WINDOWPLACEMENT restored{};
        restored.length = sizeof(restored);
        restored.showCmd = SW_HIDE;
        restored.rcNormalPosition = {placement.x, placement.y,
                                     placement.x + placement.width,
                                     placement.y + placement.height};
        if (!SetWindowPlacement(window, &restored))
            OutputDebugStringW(L"PaneDock: SetWindowPlacement failed\n");
    }
    ShowWindow(window, placement.maximized ? SW_SHOWMAXIMIZED : show_command);
    UpdateWindow(window);
    state.startup_frame_only = false;
    // PD-135: a close can still arrive before the message loop consumes the
    // deferred-realize post. `proceed` prevents startup work from continuing
    // after the main HWND has been torn down; recoverable warnings below are
    // modeless and therefore do not create another nested modal loop.
    bool proceed = !state.is_shutting_down() &&
                   !state.quit_requested;
    // Queue realization before any startup warning enters a nested modal
    // loop. The warning remains acknowledgement-only, while its modal loop
    // can dispatch this message and show the pane content behind it.
    if (proceed && state.startup_realize_pending) {
        const UINT_PTR generation = ++state.startup_realize_generation;
        if (!PostMessageW(window, kDeferredRealizeMessage, 0,
                          static_cast<LPARAM>(generation))) {
            OutputDebugStringW(
                L"PaneDock: could not queue deferred list realization\n");
            const HRESULT hr = realize_startup_panes(window, state);
            if (FAILED(hr) && !state.is_shutting_down()) {
                report_shell_failure(state, L"startup_realize", hr);
                append_startup_warning(
                    state,
                    L"PaneDock could not open the list for one or more "
                    L"panes. Some panes may be empty. " +
                        panedock::app_shell::format_shell_failure_detail(
                            hr, last_failed_pane(state)));
            }
        }
    }
    if (recovered_from_corruption &&
        session_source == panedock::core::SessionSource::backup) {
        append_startup_warning(
            state,
            L"PaneDock could not read its saved session and restored the "
            L"previous good version. Some recent changes may be missing.");
    }
    if (proceed && recovered_from_corruption &&
        session_source == panedock::core::SessionSource::default_state) {
        append_startup_warning(
            state,
            L"PaneDock could not read its saved session or its backup and "
            L"started with a default Group. Your previous Groups could not "
            L"be recovered.");
    }
    if (proceed && !clean_shutdown) {
        append_startup_warning(
            state,
            L"PaneDock did not shut down cleanly last time. If this keeps "
            L"happening, start it with --diagnostic to run without "
            L"third-party shell extensions.");
    }
    if (proceed && !state.startup_warning_message.empty()) {
        show_startup_notification(window, state);
    }
    MSG message{};
    int result = 0;
    for (;;) {
        // A close can be issued before this loop starts (e.g. the startup
        // recovery / unclean-shutdown MessageBox runs a modal loop that
        // dispatches WM_CLOSE and consumes the posted WM_QUIT). Check the flag
        // before blocking in GetMessageW, or an empty queue would leave the
        // loop blocked forever with quit_requested already true.
        if (state.quit_requested) break;
        // Wait on owned worker events as well as input. Workers never hold an
        // HWND or post to a possibly destroyed/reused window. A queued input
        // message gets a turn between bounded result drains.
        std::array<HANDLE, kMaxPaneCount * 2> events{};
        DWORD event_count = 0;
        for (const auto& pane : state.panes) {
            if (const HANDLE event = pane.content_event()) events[event_count++] = event;
            if (const HANDLE event = pane.icon_event()) events[event_count++] = event;
        }
        if (event_count != 0) {
            const DWORD wake = MsgWaitForMultipleObjectsEx(event_count, events.data(),
                INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (wake == WAIT_FAILED) { exit_code = 1; break; }
            for (auto& pane : state.panes)
                if (const HANDLE event = pane.content_event(); event &&
                    WaitForSingleObject(event, 0) == WAIT_OBJECT_0)
                    pane.process_content_results();
            for (auto& pane : state.panes)
                if (const HANDLE event = pane.icon_event(); event &&
                    WaitForSingleObject(event, 0) == WAIT_OBJECT_0)
                    pane.process_icon_results();
            if (!PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE)) continue;
        }
        result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) break;
        // The pinned-locations manager is a modeless popup: Tab moves between
        // its controls, Enter is OK and Esc is Cancel.
        if (const HWND pinned = state.pinned_locations_dialog.window();
            pinned != nullptr && IsDialogMessageW(pinned, &message)) {
            apply_pinned_locations_dialog_result(state);
            if (state.quit_requested) break;
            continue;
        }
        if (!state.is_shutting_down() &&
            has_active_group(state) &&
            pane_key_routing_applies(window, state, message)) {
            // The routing rules themselves are pure and unit-tested
            // (core::resolve_key); this loop only reads the live Win32 state
            // they need and performs what comes back.
            const auto& group = active_group(state);
            const std::size_t active = active_pane_index(group);
            panedock::core::KeyInput input{};
            input.message =
                message.message == WM_KEYDOWN
                    ? panedock::core::KeyMessage::key_down
                    : (message.message == WM_SYSKEYDOWN
                           ? panedock::core::KeyMessage::system_key_down
                           : panedock::core::KeyMessage::other);
            input.virtual_key = static_cast<unsigned>(message.wParam);
            input.control = GetKeyState(VK_CONTROL) < 0;
            input.alt = GetKeyState(VK_MENU) < 0;
            input.shift = GetKeyState(VK_SHIFT) < 0;
            input.address_bar_focused = address_bar_has_focus(state.panes) ||
                state.panes[active].list_edit_has_focus();
            input.sidebar_focused = GetFocus() == state.sidebar.window();
            input.active_pane = active;
            input.pane_count =
                panedock::core::pane_count(group.layout_template);
            input.list_focused = state.panes[active].list_has_key(message);
            const auto routing = panedock::core::resolve_key(input);

            // Enter opens the selection inside a Shell scope, as a verb
            // invocation; F2, Ctrl+A and Ctrl+Shift+N reject a nested Shell
            // call themselves.
            if (routing.action == panedock::core::KeyAction::open_selection) {
                ShellCallScope shell_call(state.reentry_guard);
                (void)state.panes[active].perform_list_key(routing.action);
                continue;
            }
            if (state.panes[active].perform_list_key(routing.action)) continue;

            bool handled = true;
            switch (routing.action) {
            case panedock::core::KeyAction::none:
                handled = false;
                break;
            case panedock::core::KeyAction::begin_group_rename:
                state.sidebar.begin_rename();
                break;
            case panedock::core::KeyAction::copy:
            case panedock::core::KeyAction::cut:
            case panedock::core::KeyAction::delete_items:
            case panedock::core::KeyAction::delete_permanently:
                (void)perform_file_command(window, state, state.panes[active],
                    file_command_for(routing.action));
                break;
            case panedock::core::KeyAction::paste:
                // The list keeps its Ctrl+V even when nothing is pastable.
                handled = perform_file_command(window, state, state.panes[active],
                    panedock::file_operations::Command::paste) ||
                    input.list_focused;
                break;
            case panedock::core::KeyAction::begin_item_rename:
            case panedock::core::KeyAction::select_all:
            case panedock::core::KeyAction::new_folder:
            case panedock::core::KeyAction::open_selection:
                break; // performed by the pane above
            case panedock::core::KeyAction::focus_next_pane:
            case panedock::core::KeyAction::focus_previous_pane:
                set_active_pane(window, state, routing.target_pane);
                break;
            case panedock::core::KeyAction::new_tab:
                state.panes[active].add_tab(default_shell_location());
                break;
            case panedock::core::KeyAction::close_tab:
                state.panes[active].close_tab(
                    state.panes[active].active_tab()->id);
                break;
            case panedock::core::KeyAction::next_tab:
                state.panes[active].cycle_active_tab(false);
                break;
            case panedock::core::KeyAction::previous_tab:
                state.panes[active].cycle_active_tab(true);
                break;
            case panedock::core::KeyAction::history_back:
                state.panes[active].navigate_history(true);
                break;
            case panedock::core::KeyAction::history_forward:
                state.panes[active].navigate_history(false);
                break;
            case panedock::core::KeyAction::navigate_up:
                state.panes[active].navigate_up();
                break;
            case panedock::core::KeyAction::refresh:
                state.panes[active].refresh_view();
                break;
            case panedock::core::KeyAction::focus_address_bar:
                state.panes[active].focus_address_bar();
                break;
            }
            if (handled) continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
        apply_pinned_locations_dialog_result(state);
        handle_transfer_close_dialog_result(state);
        // win32: a nested modal loop can consume the WM_QUIT posted in
        // WM_CLOSE/WM_DESTROY. Exit on the flag so a close issued from inside a
        // shell popup / context menu still terminates the process.
        if (state.quit_requested) break;
    }
    exit_code = result < 0 ? 1 : static_cast<int>(message.wParam);
    if (state.quit_requested) exit_code = 0;

    state.pinned_locations_dialog.destroy();
    destroy_panes(state);
    // assert() is compiled out in Release; the emitted count is what the
    // launch smoke test checks. See AppShutdownEffects::destroy_views.
    write_live_list_count(state.diagnostic_mode, state.panes);
    assert(std::none_of(state.panes.begin(), state.panes.end(),
        [](const auto& pane) { return pane.realized(); }));
    finalize_process(state);
    CloseHandle(single_instance_mutex);
    return exit_code;
}

// Re-opens this translation unit's anonymous namespace, where AppState lives.
namespace {

// PD-189: the pane proc's handler for its own children's notifications. Lives
// in the coordinator (main.cpp) so it can reach AppState; Pane only calls it
// through the PaneHost seam.
// The two entry guards mirror tab_strip_proc's: nothing is handled while the
// window is closing, and mouse messages raised during a Shell re-entry are
// deferred (PD-172 / PD-173).
std::optional<LRESULT> AppState::handle_pane_control_message(
    Pane& chrome, UINT message, WPARAM wparam, LPARAM lparam) {
    AppState* const state = this;
    const HWND pane_window = chrome.window();
    if (chrome.index() >= state->panes.size()) return std::nullopt;
    if (panedock::app_shell::child_message_blocked_while_closing(state->is_shutting_down(),
                                            message))
        return LRESULT{0};
    if (defer_shell_reentry_mouse_message(pane_window, *state, message, wparam,
                                          lparam))
        return LRESULT{0};
    // A pane's buttons are its children, so their own mouse messages never
    // reach us -- only the BN_CLICKED WM_COMMAND does, and it arrives at the
    // pane window rather than the main window that gates WM_COMMAND on
    // Shell re-entry (PD-171). Without this the back/up/new-tab buttons run
    // model-changing work re-entrantly inside a Shell call that is pumping the
    // loop, advancing the navigation generation under the in-flight request.
    // Replay it at the pane so handle_command still knows which pane it is.
    if (state->reentry_guard.in_shell_call() && message == WM_COMMAND) {
        defer_shell_reentry_message(pane_window, *state, message, wparam,
                                    lparam);
        return LRESULT{0};
    }


    switch (message) {
        case WM_COMMAND: {
            if (HIWORD(wparam) != BN_CLICKED) return std::nullopt;
            const auto control =
                panedock::app_shell::decode_pane_control(static_cast<int>(LOWORD(wparam)));
            if (!control.has_value()) return std::nullopt;
            const int id = static_cast<int>(LOWORD(wparam));
            if (!chrome.handle_command(id))
                (void)handle_global_command(state->main_window, *state, id,
                                             &chrome);
            return LRESULT{0};
        }
        case WM_DRAWITEM: {
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
            if (item == nullptr) return std::nullopt;
            auto drawing = *item;
            if (state->owner_draw_hovered_button == drawing.hwndItem)
                drawing.itemState |= ODS_HOTLIGHT;
            if (chrome.draw_control(drawing)) return LRESULT{TRUE};
            return std::nullopt;
        }
        case WM_CTLCOLOREDIT:
            return chrome.color_address_bar(reinterpret_cast<HWND>(lparam),
                                            reinterpret_cast<HDC>(wparam));
        default:
            return std::nullopt;
    }
}

}  // namespace
