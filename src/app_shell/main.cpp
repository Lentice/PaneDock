#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>

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
#include <new>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include "app_shell/diagnostic_mode.h"
#include "app_shell/pane.h"
#include "app_shell/pane_control_id.h"
#include "app_shell/pinned_locations_dialog.h"
#include "app_shell/startup_notification.h"
#include "app_shell/tab_overflow.h"
#include "app_shell/transfer_close_dialog.h"
#include "app_shell/window_helpers.h"
#include "app_shell/window_placement.h"
#include "core/layout.h"
#include "core/model.h"
#include "core/navigation.h"
#include "core/session.h"
#include "core/shutdown.h"
#include "explorer_host/explorer_host.h"
#include "file_operations/file_operations.h"
#include "resource.h"
#include "shell_core/shell_core.h"
#include "sidebar/sidebar.h"
#include "com_ref_counted.h"

namespace {

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
constexpr UINT kSessionSaveDelayMilliseconds = 500;
constexpr UINT_PTR kSessionSaveTimerId = 0xD050;
// PD-155: coalesce a geometry request that arrives while Shell is pumping the
// message loop during the current layout pass.
constexpr UINT kDeferredLayoutMessage = WM_APP + 55;
// PD-171: replay model-changing commands after an app-owned Shell call.
constexpr UINT kDeferredCommandMessage = WM_APP + 56;
constexpr UINT kDeferredTabSelectionMessage = WM_APP + 57;
constexpr int kSidebarMinimumWidth = 160;
constexpr int kSidebarMaximumWidth = 420;
constexpr std::size_t kExplorerCount = 4;
using NavigationGeneration =
    panedock::explorer_host::ExplorerHost::NavigationGeneration;
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
constexpr int kActivePaneIndicatorHeight = 3;
constexpr int kSidebarHeadingHeight = 20;
constexpr int kTabStripHeight = 31;
constexpr UINT kTabStripSelectionMessage = WM_APP + 49;
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
constexpr int kNavigationBarHeight = 28;
constexpr int kStatusBarHeight = 24;
constexpr int kNavigationButtonWidth = 32;
constexpr int kNavigationButtonOffsetX = 0;
constexpr int kNavigationButtonOffsetY = 2;
constexpr int kNavigationGlyphSize = 16;
constexpr COLORREF kStatusBarBackground = RGB(249, 250, 251);
constexpr std::array<wchar_t, 7> kNavigationGlyphs{
    L'\uE72B', L'\uE72A', L'\uE74A', L'\uE72C', L'\uE71D', L'\uE734',
    L'\uE712'};
// PD-031: rounded light-gray pill drawn behind the address bar EDIT to fake
// a rounded input box (see docs/tickets/PD-031-*.md decision 2). Radius is
// smaller than the design mock's 6px .location radius because the fixed
// kNavigationBarHeight budget (28px@96dpi) does not leave much room for an
// inset that must exceed the radius on every side while still leaving the
// EDIT control tall enough to show text.
constexpr int kAddressBarBackgroundRadius = 4;
constexpr int kAddressBarInset = 6;
// View-mode popup commands: eight IDs per pane, 360-391, kept separate from
// the navigation buttons and layout commands above.
constexpr int kViewModeMenuIdBase = 360;
constexpr std::size_t kViewModeOptionCount = 8;
constexpr int kViewModeMenuIdCount =
    static_cast<int>(kExplorerCount * kViewModeOptionCount);
constexpr int kLayoutButtonIdBase = 400;
// Pinned popup commands: four pane blocks, each with 64 custom locations and
// four fixed/action slots. The 500-771 range is separate from all controls.
constexpr int kPinnedMenuIdBase = 500;
constexpr int kPinnedMenuMaxLocationCount = 64;
constexpr int kPinnedMenuDesktopOffset = 0;
constexpr int kPinnedMenuThisPcOffset = 1;
constexpr int kPinnedMenuLocationOffset = 2;
constexpr int kPinnedMenuAddOffset =
    kPinnedMenuLocationOffset + kPinnedMenuMaxLocationCount;
constexpr int kPinnedMenuManageOffset = kPinnedMenuAddOffset + 1;
constexpr int kPinnedMenuSlotsPerPane = kPinnedMenuManageOffset + 1;
constexpr int kPinnedMenuIdCount =
    static_cast<int>(kExplorerCount) * kPinnedMenuSlotsPerPane;
constexpr UINT_PTR kTabAddTooltipIdBase = 1000;
constexpr UINT_PTR kTabScrollTooltipIdBase = 1010;
constexpr std::array<std::wstring_view, 2> kPinnedFixedParsingNames{
    L"::{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}",
    L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"};
// PD-113: fixed commands for the tab context menu, after all existing
// control and popup command ranges.
constexpr int kCloseTabId = 780;
constexpr int kCloseOtherTabsId = 781;
constexpr int kCloseAllTabsId = 782;
constexpr int kCloseTabsToRightId = 783;
constexpr int kGroupListId = 100;
constexpr int kNewGroupId = 101;
constexpr int kDuplicateGroupId = 102;
constexpr int kRenameGroupId = 103;
constexpr int kDeleteGroupId = 104;
constexpr int kMoveUpId = 105;
constexpr int kMoveDownId = 106;
// Duplicate/Rename/Delete/Move Up/Move Down keep their ids (reused as the
// group list's context-menu command ids, see WM_CONTEXTMENU) but no longer
// get their own footer button; the footer button array shrinks to New Group.
constexpr std::array<int, 1> kButtonIds{kNewGroupId};
constexpr std::array<const wchar_t*, 1> kButtonLabels{L"+ New Group"};
constexpr int kBrandBarHeight = 52;
constexpr std::array<int, 8> kLayoutButtonIds{
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
struct ViewModeOption final {
    panedock::shell_core::ViewModeSelection selection;
    const wchar_t* label;
};
constexpr std::array<ViewModeOption, kViewModeOptionCount> kViewModeOptions{{
    {{FVM_ICON, panedock::shell_core::kExtraLargeIconSize},
     L"Extra large icons"},
    {{FVM_ICON, panedock::shell_core::kLargeIconSize}, L"Large icons"},
    {{FVM_ICON, panedock::shell_core::kMediumIconSize}, L"Medium icons"},
    {{FVM_ICON, panedock::shell_core::kSmallIconSize}, L"Small icons"},
    {{FVM_LIST, -1}, L"List"},
    {{FVM_DETAILS, -1}, L"Details"},
    {{FVM_TILE, -1}, L"Tiles"},
    {{FVM_CONTENT, -1}, L"Content"},
}};
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

void write_live_view_count(bool diagnostic_mode) noexcept {
    if (!diagnostic_mode) return;
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE) return;

    constexpr char prefix[] = "panedock.live_view_count=";
    std::array<char, 64> line{};
    std::copy_n(prefix, sizeof(prefix) - 1, line.begin());
    const auto converted = std::to_chars(
        line.data() + (sizeof(prefix) - 1), line.data() + line.size() - 1,
        panedock::explorer_host::live_view_count());
    if (converted.ec != std::errc{}) return;
    *converted.ptr = '\n';
    DWORD written = 0;
    (void)WriteFile(output, line.data(),
                    static_cast<DWORD>(converted.ptr - line.data() + 1),
                    &written, nullptr);
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

struct AppState {
    // The legacy names below are references into the reducer state. Keeping
    // them avoids a second pane-wide mechanical rewrite while making the
    // reducer the only owner of shutdown transition data.
    panedock::core::ShutdownSequence shutdown_sequence;
    bool& quit_requested = shutdown_sequence.state().quit_requested;
    bool& closing_ = shutdown_sequence.state().closing_;
    bool& shutdown_prompt_active =
        shutdown_sequence.state().shutdown_prompt_active;
    bool& shutdown_save_attempted =
        shutdown_sequence.state().shutdown_save_attempted;
    bool& shutdown_clean_marker_armed =
        shutdown_sequence.state().shutdown_clean_marker_armed;
    bool& main_window_destroyed =
        shutdown_sequence.state().main_window_destroyed;
    bool& end_session_pending = shutdown_sequence.state().end_session_pending;
    unsigned& shell_call_depth = shutdown_sequence.state().shell_call_depth;
    bool& shutdown_deferred = shutdown_sequence.state().shutdown_deferred;
    bool& shutdown_message_queued =
        shutdown_sequence.state().shutdown_message_queued;
    bool& file_operation_call_active =
        shutdown_sequence.state().file_operation_call_active;
    bool& file_operation_in_progress =
        shutdown_sequence.state().file_operation_in_progress;
    bool& close_after_file_operation =
        shutdown_sequence.state().close_after_file_operation;
    bool& cancel_file_operation =
        shutdown_sequence.state().cancel_file_operation;
    // PD-093: startup shows the active Shell view first; the posted message
    // realizes the remaining visible panes after the window is interactive.
    bool startup_realize_pending{};
    // WM_CREATE only lays out the frame. Shell work starts after the top-level
    // window has been shown, so a slow active location cannot hide the UI.
    bool startup_frame_only{};
    UINT_PTR startup_realize_generation{};
    panedock::core::ApplicationState application;
    panedock::core::SessionDocument session_document;
    std::filesystem::path session_directory;
    HWND main_window{nullptr};
    bool diagnostic_mode{};
    bool session_dirty{};
    panedock::app_shell::TransferCloseDialog transfer_close_dialog;
    // Set by WM_CREATE when the Shell view cannot be opened. Never raised as a
    // modal MessageBox from inside WM_CREATE (that nested loop could dispatch a
    // WM_CLOSE to a half-created HWND); shown by wWinMain after create returns.
    std::wstring startup_error_message;
    // A recoverable startup problem the window can still open with (e.g. one
    // pane's Shell view could not be realized, or tab drag-and-drop failed).
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
    struct TabDrag final {
        HWND strip{nullptr};
        std::size_t pane_index{};
        std::size_t source_index{};
        std::string tab_id;
        POINT start{};
        bool dragging{};
        std::optional<std::size_t> target_pane_index;
        std::optional<std::size_t> target_index;
    };
    std::optional<TabDrag> tab_drag;
    struct GroupDrag final {
        HWND list{nullptr};
        std::size_t source_index{};
        std::string group_id;
        POINT start{};
        bool dragging{};
        std::optional<std::size_t> target_index;
    };
    std::optional<GroupDrag> group_drag;
    panedock::sidebar::Sidebar sidebar;
    std::array<HWND, kButtonIds.size()> sidebar_buttons{};
    HWND group_label{nullptr};
    HFONT chrome_font{nullptr};
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
    std::array<panedock::app_shell::Pane, kExplorerCount> panes{};
    std::optional<std::size_t> tab_context_menu_pane;
    std::string tab_context_menu_tab_id;
    panedock::app_shell::PinnedLocationsDialog pinned_locations_dialog;
    std::array<std::wstring, kPinnedFixedParsingNames.size()>
        pinned_fixed_labels{};
    std::array<panedock::core::NavigationRequest, kExplorerCount>
        pending_navigation{};
    // BrowseToObject may synchronously re-enter navigation_complete while the
    // remaining panes still display the outgoing Group's folders.
    bool suppress_location_capture{};
    Microsoft::WRL::ComPtr<DragHoverTarget> sidebar_drag_target;
};

void begin_shutdown(HWND window, AppState& state, bool allow_keep_open) noexcept;
void drag_state_changed(AppState& state, bool entering) noexcept;

void finish_shell_call(AppState& state) noexcept {
    const auto action = state.shutdown_sequence.step(
        panedock::core::ShutdownEvent::shell_call_left);
    if (action != panedock::core::ShutdownAction::defer ||
        state.shutdown_message_queued || state.main_window == nullptr)
        return;
    state.shutdown_sequence.step(
        panedock::core::ShutdownEvent::deferred_shutdown_queued);
    if (PostMessageW(state.main_window, kDeferredShutdownMessage, 0, 0))
        return;
    OutputDebugStringW(
        L"PaneDock: could not queue deferred shutdown\n");
    state.shutdown_sequence.step(
        panedock::core::ShutdownEvent::deferred_shutdown_queue_failed);
    if (IsWindow(state.main_window))
        begin_shutdown(state.main_window, state,
                       !state.end_session_pending);
}

class ShellCallScope final {
public:
    explicit ShellCallScope(AppState& state) noexcept : state_(state) {
        state_.shutdown_sequence.step(
            panedock::core::ShutdownEvent::shell_call_entered);
    }

    ~ShellCallScope() noexcept {
        finish_shell_call(state_);
    }

    ShellCallScope(const ShellCallScope&) = delete;
    ShellCallScope& operator=(const ShellCallScope&) = delete;

private:
    AppState& state_;
};

void defer_shell_reentry_message(HWND window, UINT message, WPARAM wparam,
                                 LPARAM lparam) noexcept {
    if (PostMessageW(window, message, wparam, lparam)) return;
    OutputDebugStringW(
        L"PaneDock: could not queue Shell re-entry interaction\n");
}

bool defer_shell_reentry_mouse_message(HWND window, AppState& state,
                                       UINT message, WPARAM wparam,
                                       LPARAM lparam) noexcept {
    if (state.shell_call_depth == 0 ||
        (message != WM_LBUTTONDOWN && message != WM_LBUTTONDBLCLK &&
         message != WM_LBUTTONUP))
        return false;
    defer_shell_reentry_message(window, message, wparam, lparam);
    return true;
}

void app_shell_call_state_changed(void* context, bool entering) noexcept {
    if (context == nullptr) return;
    auto& state = *static_cast<AppState*>(context);
    if (entering) {
        state.shutdown_sequence.step(
            panedock::core::ShutdownEvent::shell_call_entered);
    } else {
        finish_shell_call(state);
    }
}

void revoke_drag_hover_targets(AppState& state) noexcept {
    state.sidebar.revoke_drag_drop();
    state.sidebar_drag_target.Reset();
    for (auto& pane : state.panes) pane.revoke_drag_hover_target();
}

bool flush_session_file(const std::filesystem::path& path) noexcept {
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const BOOL flushed = FlushFileBuffers(file);
    const BOOL closed = CloseHandle(file);
    return flushed != FALSE && closed != FALSE;
}

panedock::core::ShellLocation location(std::wstring parsing_name) {
    return {std::move(parsing_name), {}, {}};
}

panedock::core::ShellLocation default_shell_location() {
    return location(std::wstring(kPinnedFixedParsingNames[1]));
}

std::wstring display_text_for_parsing_name(
    AppState& state, std::wstring_view parsing_name) {
    if (!parsing_name.starts_with(L"::")) return std::wstring(parsing_name);

    ShellCallScope shell_call(state);
    return panedock::shell_core::display_text_for_parsing_name(parsing_name);
}

panedock::core::ApplicationState default_application_state() {
    panedock::core::GroupState group;
    group.id = "default";
    group.name = L"Group 1";
    group.layout_template = panedock::core::LayoutTemplate::four_pane_grid;
    group.divider_ratios = panedock::core::default_divider_ratios(
        group.layout_template);
    panedock::core::reserve_panes(group);
    for (std::size_t index = 0; index < kExplorerCount; ++index) {
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

panedock::core::GroupState& active_group(AppState& state) {
    const auto group = std::find_if(
        state.application.groups.begin(), state.application.groups.end(),
        [&](const auto& candidate) {
            return candidate.id == state.application.active_group_id;
        });
    assert(group != state.application.groups.end());
    return *group;
}

const panedock::core::GroupState& active_group(const AppState& state) {
    const auto group = std::find_if(
        state.application.groups.begin(), state.application.groups.end(),
        [&](const auto& candidate) {
            return candidate.id == state.application.active_group_id;
        });
    assert(group != state.application.groups.end());
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
    const auto pane = std::find_if(
        group.panes.begin(), group.panes.end(), [&](const auto& candidate) {
            return candidate.id == group.active_pane_id;
        });
    assert(pane != group.panes.end());
    return static_cast<std::size_t>(pane - group.panes.begin());
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

NavigationGeneration begin_navigation(AppState& state, std::size_t pane_index) {
    auto& request = state.pending_navigation[pane_index];
    auto& group = active_group(state);
    auto& tab = active_tab(group.panes[pane_index]);
    request.generation = state.panes[pane_index].host().begin_navigation();
    request.group_id = group.id;
    request.tab_id = tab.id;
    return request.generation;
}

HRESULT navigate_pane(AppState& state, std::size_t pane_index,
                      const panedock::core::ShellLocation& location) {
    return state.panes[pane_index].host().navigate(
        location, begin_navigation(state, pane_index));
}

HRESULT navigate_up_pane(AppState& state, std::size_t pane_index) {
    return state.panes[pane_index].host().navigate_up(
        begin_navigation(state, pane_index));
}

bool navigation_request_is_current(AppState& state, std::size_t pane_index,
                                   NavigationGeneration generation) {
    if (pane_index >= kExplorerCount || !has_active_group(state) ||
        pane_index >= active_group(state).panes.size())
        return false;

    auto& request = state.pending_navigation[pane_index];
    if (generation < request.generation) return false;
    auto& group = active_group(state);
    auto& tab = active_tab(group.panes[pane_index]);
    if (generation > request.generation) {
        request.generation = generation;
        request.group_id = group.id;
        request.tab_id = tab.id;
    }
    return panedock::core::navigation_request_matches(
        request, generation, group.id, tab.id);
}

// plan_realization takes a snapshot of which panes currently hold a live
// view; Pane owns that flag now (PD-183), so callers collect it here rather
// than plan_realization reaching into Pane itself.
std::array<bool, kExplorerCount> realized_flags(const AppState& state) noexcept {
    std::array<bool, kExplorerCount> flags{};
    for (std::size_t index = 0; index < kExplorerCount; ++index)
        flags[index] = state.panes[index].realized();
    return flags;
}

bool navigate_realized_panes(
    AppState& state, const panedock::core::GroupState& group) noexcept {
    const auto plan = panedock::core::plan_realization(
        group, group.layout_template, realized_flags(state),
        panedock::core::RealizationMode::group_switch);
    const bool previous_suppression = state.suppress_location_capture;
    state.suppress_location_capture = true;
    for (const std::size_t pane : plan.navigate) {
        {
            ShellCallScope shell_call(state);
            state.panes[pane].host().navigate(
                active_tab(group.panes[pane]).location,
                begin_navigation(state, pane));
        }
        if (state.shutdown_deferred || state.closing_) {
            state.suppress_location_capture = previous_suppression;
            return false;
        }
    }
    state.suppress_location_capture = previous_suppression;
    return true;
}

RECT to_win32_rect(const panedock::core::PaneRect& rect) noexcept {
    return {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
}

RECT to_win32_rect(const panedock::app_shell::TabStripRect& rect) noexcept {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

RECT client_rect(HWND window) noexcept {
    RECT rect{};
    GetClientRect(window, &rect);
    return rect;
}

int scaled_value(HWND window, int value) noexcept {
    return std::max(1, MulDiv(value, static_cast<int>(GetDpiForWindow(window)),
                              96));
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

int current_sidebar_width(HWND window, const AppState& state) noexcept {
    const RECT client = client_rect(window);
    return std::min(static_cast<int>(client.right - client.left),
                    scaled_value(window, state.application.sidebar_width));
}

class WindowPositionBatch final {
public:
    WindowPositionBatch() noexcept
        : handle_(BeginDeferWindowPos(static_cast<int>(kCapacity))) {
    }

    ~WindowPositionBatch() {
        if (handle_ != nullptr) (void)EndDeferWindowPos(handle_);
    }

    WindowPositionBatch(const WindowPositionBatch&) = delete;
    WindowPositionBatch& operator=(const WindowPositionBatch&) = delete;

    void position(HWND window, HWND insert_after, const RECT& rect,
                  UINT flags) noexcept {
        if (window == nullptr) return;
        if (entry_count_ == entries_.size()) {
            handle_ = nullptr;
            (void)SetWindowPos(window, insert_after, rect.left, rect.top,
                               rect.right - rect.left, rect.bottom - rect.top,
                               flags);
            return;
        }
        entries_[entry_count_++] = {window, insert_after, rect, flags};
        if (handle_ == nullptr) return;
        const HDWP next = DeferWindowPos(
            handle_, window, insert_after, rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top, flags);
        if (next == nullptr) handle_ = nullptr;
        else handle_ = next;
    }

    bool active() const noexcept { return handle_ != nullptr; }
    HDWP* handle() noexcept {
        return handle_ == nullptr ? nullptr : &handle_;
    }

    bool commit() noexcept {
        if (handle_ != nullptr) {
            const HDWP handle = handle_;
            handle_ = nullptr;
            if (EndDeferWindowPos(handle) != FALSE) return true;
        }
        for (std::size_t index = 0; index < entry_count_; ++index) {
            const Entry& entry = entries_[index];
            (void)SetWindowPos(
                entry.window, entry.insert_after, entry.rect.left,
                entry.rect.top, entry.rect.right - entry.rect.left,
                entry.rect.bottom - entry.rect.top, entry.flags);
        }
        return false;
    }

private:
    static constexpr std::size_t kCapacity = 128;
    struct Entry final {
        HWND window{nullptr};
        HWND insert_after{nullptr};
        RECT rect{};
        UINT flags{};
    };

    HDWP handle_{};
    std::array<Entry, kCapacity> entries_{};
    std::size_t entry_count_{};
};

void position_window(WindowPositionBatch* batch, HWND window,
                     const RECT& rect, UINT flags) noexcept {
    if (batch != nullptr) {
        batch->position(window, nullptr, rect, flags);
        return;
    }
    (void)SetWindowPos(window, nullptr, rect.left, rect.top,
                       rect.right - rect.left, rect.bottom - rect.top, flags);
}

// PD-031: the navigation row's geometry (tab-strip height, back/forward/up
// button width, and the rect the address bar background/EDIT occupy) is
// needed both by apply_layout (to SetWindowPos the real child windows) and
// by paint_client_background (to draw the rounded background behind the
// EDIT). Factored into one function so the two call sites cannot drift out
// of sync with each other (see PD-031 scope item 3).
struct NavigationGeometry {
    int navigation_top;
    int navigation_height;
    int button_width;
    // Full-width rect the address bar occupies before EDIT is inset into it;
    // this is also the rect the rounded background pill is painted into.
    RECT address_background;
};

NavigationGeometry navigation_geometry(HWND window, RECT pane_rect) noexcept {
    const int strip_height = scaled_value(window, kTabStripHeight);
    const int actual_strip_height = std::min(
        strip_height, static_cast<int>(pane_rect.bottom - pane_rect.top));
    const int navigation_top = pane_rect.top + actual_strip_height;
    const int navigation_height = std::min(
        scaled_value(window, kNavigationBarHeight),
        std::max(0, static_cast<int>(pane_rect.bottom) - navigation_top));
    const int pane_width = pane_rect.right - pane_rect.left;
    const int button_width = std::min(
        scaled_value(window, kNavigationButtonWidth), pane_width / 7);
    const int button_offset_x = scaled_value(window, kNavigationButtonOffsetX);
    const int address_left = pane_rect.left + button_width * 6 + button_offset_x;
    const RECT address_background{address_left, navigation_top,
                                  pane_rect.right,
                                  navigation_top + navigation_height};
    return {navigation_top, navigation_height, button_width,
            address_background};
}

// Insets a rect on all four sides by `inset`, clamping so it never inverts.
RECT inset_rect(RECT rect, int inset) noexcept {
    rect.left = std::min(rect.right, rect.left + inset);
    rect.top = std::min(rect.bottom, rect.top + inset);
    rect.right = std::max(rect.left, rect.right - inset);
    rect.bottom = std::max(rect.top, rect.bottom - inset);
    return rect;
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
    if ((item.itemState & ODS_FOCUS) != 0) DrawFocusRect(item.hDC, &button);
}

void draw_layout_segment_background(HDC dc, RECT rect, UINT dpi) noexcept {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    const int radius = std::max(
        1, MulDiv(kAddressBarBackgroundRadius, static_cast<int>(dpi), 96));
    fill_rounded_rect(dc, rect, radius, RGB(251, 252, 253),
                      RGB(217, 225, 234));
}

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

void release_navigation_icon_font() noexcept {
    HFONT& font = navigation_icon_font(nullptr);
    if (font != nullptr) {
        DeleteObject(font);
        font = nullptr;
    }
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
        HBRUSH background = CreateSolidBrush(kTabAddHoverBackground);
        HPEN border = CreatePen(PS_SOLID,
                                scaled_value(item.hwndItem, 1),
                                kTabAddBorder);
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
                               : blend_with_footer ? kTabAddGlyph
                                                   : RGB(90, 102, 122);
    const int size =
        std::max(4, scaled_value(item.hwndItem, kNavigationGlyphSize));
    if (glyph_kind < kNavigationGlyphs.size() &&
        draw_navigation_font_glyph(item, kNavigationGlyphs[glyph_kind],
                                   color)) {
        if ((item.itemState & ODS_FOCUS) != 0)
            DrawFocusRect(item.hDC, &item.rcItem);
        return;
    }

    // The font-failure path keeps all navigation controls visible without the
    // platform icon font.
    draw_navigation_fallback_glyph(item, glyph_kind, color, size);
    if ((item.itemState & ODS_FOCUS) != 0) DrawFocusRect(item.hDC, &item.rcItem);
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
    if ((item.itemState & ODS_FOCUS) != 0) DrawFocusRect(item.hDC, &item.rcItem);
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
        kStatusBarHeight - 2 * kTabAddButtonVerticalInset +
            2 * kSpaceTight);
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

RECT pane_area(HWND window, const AppState& state) noexcept {
    RECT area = client_rect(window);
    area.left = std::min(area.right,
                         area.left + current_sidebar_width(window, state));
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

bool sidebar_boundary_at_point(HWND window, const AppState& state,
                               POINT point) noexcept {
    const RECT client = client_rect(window);
    const int boundary = client.left + current_sidebar_width(window, state);
    const int thickness = layout_metrics(window).divider_thickness;
    const int left = std::max(static_cast<int>(client.left),
                              boundary - thickness / 2);
    const int right = std::min(static_cast<int>(client.right),
                               boundary + thickness - thickness / 2);
    const RECT hit{left, client.top, right, client.bottom};
    return hit.right > hit.left && PtInRect(&hit, point);
}

RECT pane_content_area(HWND window, const AppState& state) noexcept {
    RECT area = pane_area(window, state);
    const LayoutMetrics metrics = layout_metrics(window);
    const int padding = scaled_value(window, kSpaceRoomy);
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    if (width < metrics.minimum_pane_width + 2 * padding ||
        height < metrics.minimum_pane_height + 2 * padding) {
        return area;
    }
    area.left += padding;
    area.top += padding;
    area.right -= padding;
    area.bottom -= padding;
    return area;
}

std::vector<panedock::core::PaneRect> layout_rects(
    HWND window, const AppState& state,
    const panedock::core::GroupState& group) {
    const RECT client = pane_content_area(window, state);
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

std::vector<Splitter> splitters(HWND window,
                                const AppState& state,
                                const panedock::core::GroupState& group) {
    const auto rects = layout_rects(window, state, group);
    const int thickness = layout_metrics(window).divider_thickness;
    switch (group.layout_template) {
        case panedock::core::LayoutTemplate::single:
            return {};
        case panedock::core::LayoutTemplate::left_right:
            return {{{rects[0].x + rects[0].width, rects[0].y,
                      rects[0].x + rects[0].width + thickness,
                      rects[0].y + rects[0].height},
                     0, true}};
        case panedock::core::LayoutTemplate::top_bottom:
            return {{{rects[0].x, rects[0].y + rects[0].height,
                      rects[0].x + rects[0].width,
                      rects[0].y + rects[0].height + thickness},
                     0, false}};
        case panedock::core::LayoutTemplate::three_pane:
            return {{{rects[0].x + rects[0].width, rects[0].y,
                      rects[0].x + rects[0].width + thickness,
                      rects[0].y + rects[0].height},
                     0, true},
                    {{rects[1].x, rects[1].y + rects[1].height,
                      rects[1].x + rects[1].width,
                      rects[1].y + rects[1].height + thickness},
                     1, false}};
        case panedock::core::LayoutTemplate::four_pane_grid:
            return {{{rects[0].x + rects[0].width, rects[0].y,
                      rects[0].x + rects[0].width + thickness,
                      rects[2].y + rects[2].height},
                     0, true},
                    {{rects[0].x, rects[0].y + rects[0].height,
                      rects[1].x + rects[1].width,
                      rects[0].y + rects[0].height + thickness},
                     1, false}};
        case panedock::core::LayoutTemplate::two_over_one:
            return {{{rects[2].x, rects[2].y - thickness,
                      rects[2].x + rects[2].width, rects[2].y},
                     0, false},
                    {{rects[0].x + rects[0].width, rects[0].y,
                      rects[0].x + rects[0].width + thickness,
                      rects[0].y + rects[0].height},
                     1, true}};
        case panedock::core::LayoutTemplate::one_over_two:
            return {{{rects[1].x, rects[1].y - thickness,
                      rects[2].x + rects[2].width, rects[1].y},
                     0, false},
                    {{rects[1].x + rects[1].width, rects[1].y,
                      rects[1].x + rects[1].width + thickness,
                      rects[1].y + rects[1].height},
                     1, true}};
        case panedock::core::LayoutTemplate::two_beside_one:
            return {{{rects[2].x - thickness, rects[2].y, rects[2].x,
                      rects[2].y + rects[2].height},
                     0, true},
                    {{rects[0].x, rects[0].y + rects[0].height,
                      rects[0].x + rects[0].width,
                      rects[0].y + rects[0].height + thickness},
                     1, false}};
    }
    return {};
}

std::optional<Splitter> splitter_at_point(
    HWND window, const AppState& state,
    const panedock::core::GroupState& group, POINT point) {
    for (const Splitter& splitter : splitters(window, state, group)) {
        if (PtInRect(&splitter.rect, point)) return splitter;
    }
    return std::nullopt;
}

void refresh_navigation_buttons(AppState& state, std::size_t pane_index) {
    if (pane_index >= kExplorerCount) return;
    const bool visible = has_active_group(state) &&
                         pane_index < active_group(state).panes.size();
    auto& chrome = state.panes[pane_index];
    if (!visible) {
        EnableWindow(chrome.back_button(), FALSE);
        EnableWindow(chrome.forward_button(), FALSE);
        EnableWindow(chrome.up_button(), FALSE);
        EnableWindow(chrome.folder_context_button(), FALSE);
        return;
    }
    const auto& tab = active_tab(active_group(state).panes[pane_index]);
    EnableWindow(chrome.back_button(),
                 !chrome.suppress_history() &&
                     panedock::core::can_navigate_tab_back(tab));
    EnableWindow(chrome.forward_button(),
                 !chrome.suppress_history() &&
                     panedock::core::can_navigate_tab_forward(tab));
    EnableWindow(chrome.up_button(), TRUE);
    EnableWindow(chrome.folder_context_button(), chrome.realized());
}

void refresh_navigation_chrome(AppState& state, std::size_t pane_index) {
    if (pane_index >= kExplorerCount) return;
    refresh_navigation_buttons(state, pane_index);
    std::wstring text;
    if (has_active_group(state) &&
        pane_index < active_group(state).panes.size()) {
        text = display_text_for_parsing_name(
            state,
            active_tab(active_group(state).panes[pane_index])
                .location.parsing_name);
    }
    SetWindowTextW(state.panes[pane_index].address_bar(), text.c_str());
}

void capture_pane_view_mode(AppState& state, std::size_t pane_index) {
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size() ||
        !state.panes[pane_index].realized()) return;
    FOLDERVIEWMODE mode{};
    int image_size = -1;
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCallScope shell_call(state);
        hr = state.panes[pane_index].host().get_view_mode(mode, &image_size);
    }
    if (state.shutdown_deferred || state.closing_) return;
    if (SUCCEEDED(hr)) {
        const std::string name = panedock::shell_core::view_mode_name(
            mode, image_size);
        if (!name.empty())
            active_tab(active_group(state).panes[pane_index]).view_mode = name;
    }
}

void capture_pane_sort(AppState& state, std::size_t pane_index) {
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size() ||
        !state.panes[pane_index].realized()) return;
    std::string column;
    bool ascending{};
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCallScope shell_call(state);
        hr = state.panes[pane_index].host().get_sort(column, ascending);
    }
    if (state.shutdown_deferred || state.closing_) return;
    if (SUCCEEDED(hr)) {
        auto& tab = active_tab(active_group(state).panes[pane_index]);
        tab.sort_column = std::move(column);
        tab.sort_ascending = ascending;
    }
}

void apply_pane_view_mode(AppState& state, std::size_t pane_index) {
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size() ||
        !state.panes[pane_index].realized()) return;
    auto& tab = active_tab(active_group(state).panes[pane_index]);
    if (const auto selection = panedock::shell_core::parse_view_mode(
            tab.view_mode);
        selection.has_value()) {
        {
            ShellCallScope shell_call(state);
            (void)state.panes[pane_index].host().set_view_mode(
                selection->mode, selection->image_size);
        }
    } else if (tab.view_mode.empty()) {
        {
            ShellCallScope shell_call(state);
            (void)state.panes[pane_index].host().set_view_mode(FVM_DETAILS);
        }
    }
    if (state.shutdown_deferred || state.closing_) return;
    capture_pane_view_mode(state, pane_index);
}

void apply_pane_sort(AppState& state, std::size_t pane_index) {
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size() ||
        !state.panes[pane_index].realized()) return;
    const auto& tab = active_tab(active_group(state).panes[pane_index]);
    if (tab.sort_column.empty()) return;
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCallScope shell_call(state);
        hr = state.panes[pane_index].host().set_sort(tab.sort_column,
                                                      tab.sort_ascending);
    }
    if (state.shutdown_deferred || state.closing_ || FAILED(hr)) return;
    capture_pane_sort(state, pane_index);
}

void refresh_status_bar(AppState& state, std::size_t pane_index) noexcept {
    if (pane_index >= state.panes.size() ||
        state.panes[pane_index].status_bar() == nullptr)
        return;
    panedock::explorer_host::ExplorerHost::ItemCounts counts;
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCallScope shell_call(state);
        hr = state.panes[pane_index].host().item_counts(counts);
    }
    if (state.shutdown_deferred || state.closing_) return;
    if (FAILED(hr)) {
        SetWindowTextW(state.panes[pane_index].status_bar(), L"");
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
    SetWindowTextW(state.panes[pane_index].status_bar(), text.c_str());
}

std::wstring tab_display_text(AppState& state,
                              const panedock::core::TabState& tab) {
    const auto& parsing_name = tab.location.parsing_name;
    const std::size_t separator = parsing_name.find_last_of(L"\\/");
    if (separator == std::wstring::npos || separator + 1 == parsing_name.size())
        return display_text_for_parsing_name(state, parsing_name);
    return parsing_name.substr(separator + 1);
}

void update_tab_strip_tooltips(AppState& state,
                               std::size_t pane_index) noexcept {
    if (state.layout_tooltip == nullptr ||
        pane_index >= state.panes.size())
        return;
    auto& chrome = state.panes[pane_index];
    const HWND strip = chrome.tab_strip();
    if (strip == nullptr) return;

    const auto& geometry = chrome.tab_geometry();
    const std::array<RECT, 3> rects{
        to_win32_rect(geometry.add_rect),
        to_win32_rect(geometry.scroll_button_rects[0]),
        to_win32_rect(geometry.scroll_button_rects[1])};
    const std::array<UINT_PTR, 3> ids{
        kTabAddTooltipIdBase + static_cast<UINT_PTR>(pane_index),
        kTabScrollTooltipIdBase + static_cast<UINT_PTR>(pane_index * 2),
        kTabScrollTooltipIdBase + static_cast<UINT_PTR>(pane_index * 2 + 1)};
    constexpr std::array<const wchar_t*, 3> texts{
        L"New tab", L"Scroll tabs left", L"Scroll tabs right"};
    const UINT message = chrome.tab_tooltips_registered()
                             ? TTM_NEWTOOLRECT
                             : TTM_ADDTOOLW;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        TOOLINFOW info{};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_SUBCLASS;
        info.hwnd = strip;
        info.uId = ids[index];
        info.rect = rects[index];
        info.lpszText = const_cast<wchar_t*>(texts[index]);
        SendMessageW(state.layout_tooltip, message, 0,
                     reinterpret_cast<LPARAM>(&info));
    }
    chrome.tab_tooltips_registered() = true;
}

void apply_tab_item_size(AppState& state, std::size_t pane_index,
                         bool reveal_active = false) {
    if (pane_index >= state.panes.size()) return;
    auto& chrome = state.panes[pane_index];
    const HWND strip = chrome.tab_strip();
    RECT client{};
    GetClientRect(strip, &client);
    const int min_width = scaled_value(strip, kTabMinWidth);
    const int max_width = scaled_value(strip, kTabMaxWidth);
    const int add_width = scaled_value(strip, kTabAddButtonWidth);
    const auto& visuals = chrome.tab_visuals();
    const int text_reserve = scaled_value(
        strip, 2 * kTabTextHorizontalPadding + kTabCloseButtonSpace);
    std::vector<int> preferred_widths;
    preferred_widths.reserve(visuals.size());
    HDC dc = GetDC(strip);
    const HFONT font = state.chrome_font;
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
    std::optional<panedock::app_shell::TabStripDragLayout> drag_layout;
    const bool foreign_placeholder =
        state.tab_drag.has_value() && state.tab_drag->dragging &&
        state.tab_drag->target_pane_index == pane_index &&
        state.tab_drag->pane_index != pane_index &&
        state.tab_drag->target_index.has_value();
    if (foreign_placeholder) {
        int placeholder_width = min_width;
        if (state.tab_drag->pane_index < state.panes.size() &&
            state.tab_drag->source_index <
                state.panes[state.tab_drag->pane_index]
                    .tab_visuals().size()) {
            const auto& source =
                state.panes[state.tab_drag->pane_index]
                    .tab_visuals()[state.tab_drag->source_index];
            SIZE size{};
            HDC measure = GetDC(strip);
            const HGDIOBJ old =
                measure == nullptr ? nullptr : SelectObject(measure, font);
            if (measure != nullptr) {
                GetTextExtentPoint32W(measure, source.text.c_str(),
                                      static_cast<int>(source.text.size()),
                                      &size);
                SelectObject(measure, old);
                ReleaseDC(strip, measure);
            }
            placeholder_width = std::clamp(
                static_cast<int>(size.cx) + text_reserve, min_width, max_width);
        }
        drag_layout = panedock::app_shell::TabStripDragLayout{
            state.tab_drag->source_index, state.tab_drag->target_index, true,
            placeholder_width};
    } else if (state.tab_drag.has_value() && state.tab_drag->dragging &&
               state.tab_drag->pane_index == pane_index &&
               !state.tab_drag->target_pane_index.has_value() &&
               state.tab_drag->target_index.has_value()) {
        drag_layout = panedock::app_shell::TabStripDragLayout{
            state.tab_drag->source_index, state.tab_drag->target_index, false,
            0};
    }

    std::optional<std::size_t> active_index;
    if (reveal_active && has_active_group(state) &&
        pane_index < active_group(state).panes.size()) {
        const auto& pane = active_group(state).panes[pane_index];
        for (std::size_t index = 0; index < pane.tabs.size(); ++index) {
            if (pane.tabs[index].id == pane.active_tab_id) {
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
        chrome.tab_geometry().scroll_offset,
        drag_layout,
        active_index};
    chrome.set_geometry(panedock::app_shell::layout_tab_strip(layout_input));
    InvalidateRect(strip, nullptr, FALSE);
    update_tab_strip_tooltips(state, pane_index);
}

void refresh_tab_strip(AppState& state, std::size_t pane_index) {
    if (pane_index >= state.panes.size()) return;
    auto& chrome = state.panes[pane_index];
    chrome.set_tab_hover(std::nullopt);
    chrome.set_scroll_hover(std::nullopt);
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size()) {
        chrome.set_tabs({});
        apply_tab_item_size(state, pane_index);
        refresh_navigation_chrome(state, pane_index);
        return;
    }

    const auto& pane = active_group(state).panes[pane_index];
    std::vector<std::wstring> labels;
    labels.reserve(pane.tabs.size());
    for (const auto& tab : pane.tabs)
        labels.push_back(tab_display_text(state, tab));
    chrome.set_tabs(labels);
    apply_tab_item_size(state, pane_index, true);
    refresh_navigation_chrome(state, pane_index);
}

void refresh_tab_strips(AppState& state) {
    for (std::size_t index = 0; index < state.panes.size(); ++index)
        refresh_tab_strip(state, index);
}

void capture_pane_location(AppState& state, std::size_t pane_index) {
    if (state.suppress_location_capture || !has_active_group(state)) return;
    auto& group = active_group(state);
    if (pane_index >= group.panes.size() ||
        !state.panes[pane_index].realized() ||
        state.panes[pane_index].host().location().parsing_name.empty())
        return;
    auto& tab = active_tab(group.panes[pane_index]);
    tab.location = state.panes[pane_index].host().location();
    capture_pane_view_mode(state, pane_index);
    if (state.shutdown_deferred || state.closing_) return;
    capture_pane_sort(state, pane_index);
    if (!tab.history.empty() && tab.history_index < tab.history.size()) {
        tab.history[tab.history_index] = tab.location;
    }
}

void capture_locations(AppState& state) {
    if (state.suppress_location_capture || !has_active_group(state)) return;
    for (std::size_t index = 0; index < active_group(state).panes.size();
         ++index) {
        capture_pane_location(state, index);
    }
}

void refresh_sidebar(AppState& state) {
    std::vector<panedock::sidebar::GroupSummary> summaries;
    summaries.reserve(state.application.groups.size());
    std::optional<std::size_t> active_index;
    for (std::size_t index = 0; index < state.application.groups.size();
         ++index) {
        const auto& group = state.application.groups[index];
        const std::size_t tab_count = std::accumulate(
            group.panes.begin(), group.panes.end(), std::size_t{0},
            [](std::size_t total, const panedock::core::PaneState& pane) {
                return total + pane.tabs.size();
            });
        summaries.push_back(
            {group.id, group.name, group.panes.size(), tab_count});
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
                    WindowPositionBatch* batch = nullptr,
                    RECT* list_rect_out = nullptr) noexcept {
    const RECT client = client_rect(window);
    const int width = current_sidebar_width(window, state);
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
    if (list_rect_out != nullptr) *list_rect_out = list_rect;
    state.sidebar.set_rect(
        list_rect, GetDpiForWindow(window),
        batch != nullptr ? batch->handle() : nullptr);
    int y = list_rect.bottom + gap;
    for (HWND button : state.sidebar_buttons) {
        position_window(batch, button,
                        RECT{margin, y, margin + std::max(0, width - 2 * margin),
                             y + button_height},
                        SWP_NOZORDER | SWP_NOACTIVATE);
        y += button_height + gap;
    }
    const RECT panes = pane_area(window, state);
    position_window(batch, state.empty_message, panes,
                    SWP_NOZORDER | SWP_NOACTIVATE);
}

void layout_header(HWND window, AppState& state,
                   WindowPositionBatch* batch = nullptr,
                   bool update_selection = true) noexcept {
    const RECT client = client_rect(window);
    const int sidebar_width = current_sidebar_width(window, state);
    const int margin = scaled_value(window, kSpaceBase);
    const int segment_gap = scaled_value(window, 1);
    const int header_height = std::min(
        scaled_value(window, kLayoutBarHeight),
        std::max(0, static_cast<int>(client.bottom - client.top)));
    const int button_height = std::min(
        scaled_value(window, kLayoutButtonHeight), header_height);
    const int button_width = std::max(
        1, std::min(scaled_value(window, kLayoutButtonWidth),
                    std::max(0, static_cast<int>(client.right) -
                                    sidebar_width - 2 * margin -
                                    (static_cast<int>(kLayoutButtonIds.size()) -
                                     1) * segment_gap) /
                        static_cast<int>(kLayoutButtonIds.size())));
    const int total_width =
        static_cast<int>(kLayoutButtonIds.size()) * button_width +
        (static_cast<int>(kLayoutButtonIds.size()) - 1) * segment_gap;
    // Right-align the whole group; if the window is too narrow to fit it
    // with room to spare on the left of the sidebar, fall back to the
    // original left-aligned start position instead of overlapping it.
    const int x_start = std::max(sidebar_width + margin,
                                 static_cast<int>(client.right) - margin -
                                     total_width);
    int x = x_start;
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
    const HFONT font = state.chrome_font;
    if (font == nullptr) return;
    set_ui_font(state.sidebar.window(), font);
    set_ui_font(state.group_label, font);
    for (HWND button : state.sidebar_buttons) set_ui_font(button, font);
    for (HWND button : state.layout_buttons) set_ui_font(button, font);
    set_ui_font(state.empty_message, font);
    for (auto& chrome : state.panes) {
        chrome.apply_font(font);
        set_ui_font(chrome.folder_context_button(), font);
    }
    state.pinned_locations_dialog.apply_font(font);
    state.startup_notification.apply_font(font);
}

void refresh_ui_font(HWND window, AppState& state) noexcept {
    const HFONT next = ui_font(window);
    if (next == nullptr) return;
    const HFONT previous = state.chrome_font;
    state.chrome_font = next;
    apply_ui_font(state);
    if (previous != nullptr) DeleteObject(previous);
}

void release_ui_font(AppState& state) noexcept {
    if (state.chrome_font != nullptr) {
        DeleteObject(state.chrome_font);
        state.chrome_font = nullptr;
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

// Shared by draw_pane_card's background/border RoundRects and the PD-040
// explorer container's SetWindowRgn clip, so the drawn corner and the
// clipped corner are always the same radius and never show a mismatch seam.
int pane_card_radius(UINT dpi) noexcept {
    return std::max(1, MulDiv(10, static_cast<int>(dpi), 96));
}

// PD-042: clip a pane's explorer container child window with rounded bottom
// corners while keeping the internal top edge square. Called whenever the
// container's rect changes (creation, WM_SIZE, WM_DPICHANGED — every
// apply_layout pass).
void apply_pane_container_region(HWND container, int width, int height,
                                 int radius) noexcept {
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

void draw_pane_card(HDC dc, RECT pane_rect, UINT dpi) noexcept {
    // Card visuals: white body, shadow one step darker (product decision 3).
    // Radius 10px@96dpi matches the design mock's .pane { border-radius:
    // 10px }. Shadow is a flat offset RoundRect, not a real blur (product
    // decision 1 — no AlphaBlend/GradientFill). The active-pane indicator is
    // the straight accent bar painted by paint_tab_strip; every card keeps
    // the same quiet-gray border so no active rounded outline is drawn here.
    const int radius = pane_card_radius(dpi);
    const int shadow_offset = std::max(1, MulDiv(2, static_cast<int>(dpi), 96));
    // The card is drawn a couple of pixels outside pane_rect on the left/
    // top/right so its rounded top corners and border are visible in the
    // padding/divider gap that already surrounds every pane rect, rather
    // than being fully hidden under the opaque tab-strip control that sits
    // flush at pane_rect's top edge (tab strip/nav row/Shell view are drawn
    // on top of this, per PD-030's architecture decision). The bottom edge
    // stays exactly at pane_rect.bottom, flush with the real Shell view
    // content — all four corners are rounded (PD-040 overrides PD-030's
    // square-bottom decision; the PD-040 explorer container clips the real
    // Shell view to the same radius so there is no seam at the bottom
    // corners).
    const int outset = std::max(1, MulDiv(2, static_cast<int>(dpi), 96));
    const RECT card{pane_rect.left - outset, pane_rect.top - outset,
                    pane_rect.right + outset, pane_rect.bottom + outset};

    RECT shadow_rect = card;
    OffsetRect(&shadow_rect, shadow_offset, shadow_offset);
    fill_rounded_rect(dc, shadow_rect, radius, RGB(235, 239, 244), CLR_NONE);
    fill_rounded_rect(dc, card, radius, RGB(255, 255, 255), CLR_NONE);

    const int border_width = std::max(1, MulDiv(1, static_cast<int>(dpi), 96));
    const COLORREF border_color = RGB(232, 237, 242);
    HPEN border_pen = CreatePen(PS_SOLID, border_width, border_color);
    if (border_pen != nullptr) {
        const HGDIOBJ old_pen = SelectObject(dc, border_pen);
        const HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, card.left, card.top, card.right, card.bottom, radius,
                  radius);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(border_pen);
    }
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

void release_address_bar_background_brush() noexcept {
    // The static above is a function-local singleton; DeleteObject is safe
    // to call on it more than once only if we null it out, but WM_DESTROY
    // fires exactly once per window, so a single delete here is sufficient.
    HBRUSH brush = address_bar_background_brush();
    if (brush != nullptr) DeleteObject(brush);
}

void draw_navigation_bar_background(HDC dc, RECT rect, UINT dpi) noexcept {
    // Rounded light-gray pill drawn behind the address bar EDIT (PD-031
    // decision 2). Colors/radius taken from the design mock's .location
    // rule (background #fbfcfd, border #d9e1ea); radius reduced from the
    // mock's 6px — see kAddressBarBackgroundRadius comment for why.
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    const int radius = std::max(
        1, MulDiv(kAddressBarBackgroundRadius, static_cast<int>(dpi), 96));
    fill_rounded_rect(dc, rect, radius, RGB(251, 252, 253),
                      RGB(217, 225, 234));
}

void paint_client_background(HWND window, HDC dc,
                             const AppState& state) noexcept {
    const RECT client = client_rect(window);
    HBRUSH canvas = CreateSolidBrush(RGB(243, 246, 249));
    if (canvas != nullptr) {
        FillRect(dc, &client, canvas);
        DeleteObject(canvas);
    }

    const int sidebar_width = current_sidebar_width(window, state);
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
    if (!state.layout_buttons.empty() &&
        GetWindowRect(state.layout_buttons.front(), &layout_group) &&
        GetWindowRect(state.layout_buttons.back(), &last_layout_button)) {
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

    if (has_active_group(state)) {
        const auto& group = active_group(state);
        const auto rects = layout_rects(window, state, group);
        const UINT dpi = GetDpiForWindow(window);
        const std::size_t visible =
            std::min(group.panes.size(), rects.size());
        for (std::size_t index = 0; index < visible; ++index) {
            const RECT pane_rect = to_win32_rect(rects[index]);
            draw_pane_card(dc, pane_rect, dpi);
            const NavigationGeometry geometry =
                navigation_geometry(window, pane_rect);
            draw_navigation_bar_background(dc, geometry.address_background,
                                           dpi);
        }
    }
}

void cancel_session_save_timer(const AppState& state) noexcept {
    if (state.main_window != nullptr)
        KillTimer(state.main_window, kSessionSaveTimerId);
}

void append_startup_warning(AppState& state, std::wstring_view warning) {
    state.startup_notification.append_warning(warning);
}

bool save_now(AppState& state, bool clean_shutdown = false,
              bool force_during_transition = false) noexcept {
    // Shutdown must still persist dirty model state if Shell re-entry leaves
    // the Group-switch capture guard active.
    if (state.suppress_location_capture && !force_during_transition)
        return false;
    state.session_dirty = true;
    capture_locations(state);
    state.session_document.application = state.application;
    state.session_document.clean_shutdown = clean_shutdown;
    if (!panedock::core::write_session(state.session_directory,
                                       state.session_document,
                                       flush_session_file)) {
        OutputDebugStringW(L"PaneDock: session persistence failed\n");
        return false;
    }
    state.session_dirty = false;
    cancel_session_save_timer(state);
    return true;
}

void schedule_session_save(AppState& state) noexcept {
    state.session_dirty = true;
    if (state.main_window == nullptr) return;
    if (SetTimer(state.main_window, kSessionSaveTimerId,
                 kSessionSaveDelayMilliseconds, nullptr) == 0) {
        OutputDebugStringW(L"PaneDock: session save timer failed\n");
        (void)save_now(state);
    }
}

void apply_pinned_locations_dialog_result(AppState& state) noexcept {
    auto result = state.pinned_locations_dialog.take_result();
    if (!result.has_value() ||
        result->pinned_locations == state.application.pinned_locations)
        return;
    state.application.pinned_locations = std::move(result->pinned_locations);
    schedule_session_save(state);
}

void show_pinned_locations_manager(HWND owner, AppState& state) {
    std::vector<std::wstring> display_labels;
    display_labels.reserve(state.application.pinned_locations.size());
    for (const auto& pinned : state.application.pinned_locations)
        display_labels.push_back(
            display_text_for_parsing_name(state, pinned.parsing_name));
    state.pinned_locations_dialog.show(owner, state.application,
                                      std::move(display_labels),
                                      state.chrome_font);
}

void handle_navigation_complete(
    AppState& state, std::size_t pane_index,
    NavigationGeneration generation,
    const panedock::core::ShellLocation& new_location) {
    if (state.shutdown_deferred || state.closing_) return;
    if (!navigation_request_is_current(state, pane_index, generation)) return;
    auto& tab = active_tab(active_group(state).panes[pane_index]);
    auto completed_location = new_location;
    if (state.panes[pane_index].suppress_history()) {
        state.panes[pane_index].set_suppress_history(false);
        tab.location = std::move(completed_location);
        if (!tab.history.empty() && tab.history_index < tab.history.size()) {
            tab.history[tab.history_index] = tab.location;
        }
    } else {
        panedock::core::record_navigation(tab, std::move(completed_location));
    }
    apply_pane_view_mode(state, pane_index);
    if (state.shutdown_deferred || state.closing_) return;
    apply_pane_sort(state, pane_index);
    if (state.shutdown_deferred || state.closing_) return;
    refresh_tab_strip(state, pane_index);
    schedule_session_save(state);
}

void handle_navigation_failed(AppState& state, std::size_t pane_index,
                              NavigationGeneration generation) {
    if (!navigation_request_is_current(state, pane_index, generation)) return;
    // A pending back/forward navigation that fails asynchronously must still
    // release the suppression flag, or those buttons stay disabled forever.
    state.panes[pane_index].set_suppress_history(false);
    refresh_navigation_buttons(state, pane_index);
}

// PD-183: Pane::destroy() now folds ExplorerHost::destroy() into its own
// fixed destroy order (see pane.cpp), so tearing down every pane's chrome
// and Shell view is this one loop everywhere shutdown needs it — each
// destroy() call is still wrapped in its own ShellCallScope since it may
// perform a Shell call (ExplorerHost::destroy()).
void destroy_panes(AppState& state) noexcept {
    for (auto& chrome : state.panes) {
        ShellCallScope shell_call(state);
        chrome.destroy();
    }
    write_live_view_count(state.diagnostic_mode);
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
            state_.closing_ || state_.shutdown_deferred ||
            state_.main_window == nullptr)
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
    // A close torn down the Shell views; any message re-dispatched by the
    // IExplorerBrowser::Destroy pump must not re-create a live view while the
    // parent HWND is being destroyed (§9.4). Guard the shared entry, not every
    // caller.
    if (state.closing_ || state.shutdown_deferred) return E_ABORT;
    if (state.layout_in_progress) {
        state.layout_pending = true;
        return S_OK;
    }
    LayoutPassScope layout_scope(window, state);
    WindowPositionBatch positions;
    // DeferWindowPos requires every window in a batch to share one parent.
    // The app chrome and each ExplorerBrowser therefore get separate native
    // batches, committed by this one layout pass.
    std::array<std::optional<WindowPositionBatch>, kExplorerCount>
        explorer_positions;
    RECT sidebar_list_rect{};
    layout_sidebar(window, state, &positions, &sidebar_list_rect);
    layout_header(window, state, &positions, recompute_content);
    if (!has_active_group(state)) {
        for (std::size_t index = 0; index < state.panes.size(); ++index) {
            {
                ShellCallScope shell_call(state);
                state.panes[index].host().set_visible(false);
            }
            if (state.shutdown_deferred || state.closing_) return E_ABORT;
            state.panes[index].set_visible(false);
            ShowWindow(state.panes[index].folder_context_button(), SW_HIDE);
            EnableWindow(state.panes[index].folder_context_button(), FALSE);
        }
        ShowWindow(state.empty_message, SW_SHOW);
        for (auto& chrome : state.panes)
            chrome.laid_out_pane_rect().reset();
        if (!positions.commit())
            state.sidebar.set_rect(sidebar_list_rect, GetDpiForWindow(window));
        InvalidateRect(window, nullptr, FALSE);
        if (recompute_content) write_live_view_count(state.diagnostic_mode);
        return S_OK;
    }
    ShowWindow(state.empty_message, SW_HIDE);
    auto& group = active_group(state);
    const auto rects = layout_rects(window, state, group);
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
    const int container_radius = pane_card_radius(dpi);
    struct LayoutFailure final {
        HRESULT result;
        std::size_t pane_index;
    };
    std::optional<LayoutFailure> first_failure;
    std::array<bool, kExplorerCount> changed_panes{};
    std::array<std::optional<RECT>, kExplorerCount> pending_shell_rects{};
    std::array<bool, kExplorerCount> shell_positions_deferred{};
    std::array<RECT, kExplorerCount> container_rects{};
    for (std::size_t index = 0; index < state.panes.size(); ++index) {
        auto& chrome = state.panes[index];
        const bool visible =
            index < panedock::core::pane_count(group.layout_template);
        RECT pane_rect{};
        bool pane_geometry_changed = false;
        if (visible) {
            pane_rect = to_win32_rect(rects[index]);
            pane_geometry_changed = chrome.set_rect(pane_rect, nullptr);
            changed_panes[index] = pane_geometry_changed;
            const int strip_height = scaled_value(window, kTabStripHeight);
            const int actual_strip_height =
                std::min(strip_height, static_cast<int>(pane_rect.bottom -
                                                        pane_rect.top));
            if (pane_geometry_changed) {
                position_window(
                    &positions, chrome.tab_strip(),
                    RECT{pane_rect.left, pane_rect.top, pane_rect.right,
                         pane_rect.top + actual_strip_height},
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            const NavigationGeometry geometry =
                navigation_geometry(window, pane_rect);
            const int navigation_top = geometry.navigation_top;
            const int navigation_height = geometry.navigation_height;
            const std::array<HWND, 6> buttons{
                chrome.back_button(), chrome.forward_button(),
                chrome.up_button(), chrome.refresh_button(),
                chrome.view_mode_button(), chrome.pinned_button()};
            const int button_offset_x =
                scaled_value(window, kNavigationButtonOffsetX);
            const int button_offset_y =
                scaled_value(window, kNavigationButtonOffsetY);
            const int button_height = navigation_height - button_offset_y;
            int x = pane_rect.left + button_offset_x;
            for (HWND button : buttons) {
                if (pane_geometry_changed) {
                    position_window(
                        &positions, button,
                        RECT{x, navigation_top + button_offset_y,
                             x + geometry.button_width,
                             navigation_top + button_offset_y + button_height},
                        SWP_NOZORDER | SWP_NOACTIVATE);
                }
                x += geometry.button_width;
            }
            // PD-031: EDIT is inset well inside the rounded background pill
            // (drawn by draw_navigation_bar_background) so its square
            // corners sit hidden under the pill's rounded corners — see
            // kAddressBarInset's comment for why the inset exceeds the
            // background's radius.
            const RECT address_rect = inset_rect(
                geometry.address_background,
                scaled_value(window, kAddressBarInset));
            if (pane_geometry_changed) {
                position_window(&positions, chrome.address_bar(),
                                address_rect,
                                SWP_NOZORDER | SWP_NOACTIVATE);
            }
            RECT rect = pane_rect;
            rect.top = navigation_top + navigation_height;
            const int status_height = std::min(
                scaled_value(window, kStatusBarHeight),
                std::max(0, static_cast<int>(rect.bottom - rect.top)));
            const int footer_top = static_cast<int>(rect.bottom) - status_height;
            const int pane_width = std::max(
                0, static_cast<int>(pane_rect.right - pane_rect.left));
            const int footer_vertical_inset = std::min(
                scaled_value(window, kTabAddButtonVerticalInset),
                std::max(0, (status_height - 1) / 2));
            const int footer_button_top = footer_top + std::max(
                0, footer_vertical_inset - scaled_value(window, 1));
            const int footer_button_bottom = std::max(
                footer_button_top,
                std::min(static_cast<int>(rect.bottom),
                         static_cast<int>(rect.bottom) - footer_vertical_inset +
                             scaled_value(window, 3)));
            const int footer_horizontal_inset = std::min(
                scaled_value(window, kSpaceTight),
                std::max(0, (pane_width - 1) / 2));
            const int desired_footer_button_width = std::max(
                1, footer_button_bottom - footer_button_top);
            const int footer_button_width = std::min(
                desired_footer_button_width,
                std::max(1, pane_width - 2 * footer_horizontal_inset));
            const int footer_button_right =
                static_cast<int>(pane_rect.right) - footer_horizontal_inset;
            const int footer_button_left = std::max(
                static_cast<int>(pane_rect.left) + footer_horizontal_inset,
                static_cast<int>(footer_button_right - footer_button_width));
            const RECT status_rect{
                rect.left, footer_top, rect.right, rect.bottom};
            // Keep the action inside the footer's visual bounds so its hover
            // fill cannot cover the separator or pane card border.
            const RECT footer_button_rect{footer_button_left,
                                          footer_button_top,
                                          footer_button_right,
                                          footer_button_bottom};
            if (pane_geometry_changed) {
                position_window(&positions, chrome.status_bar(),
                                status_rect,
                                SWP_NOZORDER | SWP_NOACTIVATE);
                position_window(&positions,
                                chrome.folder_context_button(),
                                footer_button_rect,
                                SWP_NOZORDER | SWP_NOACTIVATE);
            }
            chrome.set_visible(true);
            ShowWindow(chrome.folder_context_button(), SW_SHOW);
            // The status bar spans the full footer for its separator; keep
            // the inset action above that sibling so it remains drawable.
            SetWindowPos(chrome.folder_context_button(), HWND_TOP, 0, 0,
                         0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            EnableWindow(chrome.folder_context_button(), chrome.realized());
            rect.bottom -= status_height;
            // PD-040: the container is the real parent HWND passed to
            // ExplorerHost::initialize now, positioned/sized at `rect` in
            // main-window coordinates; the browser itself is initialized
            // with a container-local, zero-based rect. SetWindowRgn on the
            // container (not on IExplorerBrowser's own HWND) is what gives
            // the real Shell view rounded corners that line up with
            // draw_pane_card's background — see pane_card_radius/
            // apply_pane_container_region.
            const int container_width = rect.right - rect.left;
            const int container_height = rect.bottom - rect.top;
            if (pane_geometry_changed) {
                position_window(&positions, chrome.explorer_container(),
                                rect, SWP_NOZORDER | SWP_NOACTIVATE);
                container_rects[index] = rect;
            }
            const RECT local_rect{0, 0, container_width, container_height};
            if (plan_contains(realization_plan.realize, index)) {
                chrome.host().set_shell_call_callback(
                    &state, app_shell_call_state_changed);
                HRESULT hr = E_UNEXPECTED;
                {
                    ShellCallScope shell_call(state);
                    hr = chrome.realize(
                        local_rect, active_tab(group.panes[index]).location);
                }
                if (state.shutdown_deferred || state.closing_) return E_ABORT;
                if (FAILED(hr)) {
                    if (!first_failure.has_value())
                        first_failure = LayoutFailure{hr, index};
                    continue;
                }
                refresh_navigation_buttons(state, index);
                {
                    ShellCallScope shell_call(state);
                    (void)SHAutoComplete(chrome.address_bar(),
                                         SHACF_FILESYS_DIRS);
                }
                if (state.shutdown_deferred || state.closing_) return E_ABORT;
                try {
                    chrome.host().set_navigation_callback(
                        [&state, index](NavigationGeneration generation,
                            const panedock::core::ShellLocation& new_location) {
                            handle_navigation_complete(state, index, generation,
                                                       new_location);
                        });
                    chrome.host().set_navigation_failed_callback(
                        [&state, index](NavigationGeneration generation) {
                            handle_navigation_failed(state, index, generation);
                        });
                    chrome.host().set_selection_changed_callback(
                        [&state, index]() { refresh_status_bar(state, index); });
                    apply_pane_view_mode(state, index);
                    if (state.shutdown_deferred || state.closing_)
                        return E_ABORT;
                    apply_pane_sort(state, index);
                    if (state.shutdown_deferred || state.closing_)
                        return E_ABORT;
                } catch (...) {
                    return E_OUTOFMEMORY;
                }
            } else if (pane_geometry_changed) {
                pending_shell_rects[index] = local_rect;
                explorer_positions[index].emplace();
                if (explorer_positions[index]->active()) {
                    shell_positions_deferred[index] = true;
                    ShellCallScope shell_call(state);
                    chrome.host().set_rect(
                        local_rect, explorer_positions[index]->handle());
                } else {
                    ShellCallScope shell_call(state);
                    chrome.host().set_rect(local_rect, nullptr);
                }
                if (state.shutdown_deferred || state.closing_) return E_ABORT;
            }
            if (recompute_content) {
                refresh_status_bar(state, index);
                if (state.shutdown_deferred || state.closing_) return E_ABORT;
            }
            // Pane owns the outer-rect cache; the native child batches
            // are committed below before regions are applied.
        } else {
            if (plan_contains(realization_plan.derealize, index)) {
                {
                    ShellCallScope shell_call(state);
                    chrome.derealize();
                }
                if (state.shutdown_deferred || state.closing_) return E_ABORT;
            }
            chrome.set_visible(false);
            ShowWindow(chrome.folder_context_button(), SW_HIDE);
            EnableWindow(chrome.folder_context_button(), FALSE);
        }
        {
            ShellCallScope shell_call(state);
            chrome.host().set_visible(visible);
        }
        if (state.shutdown_deferred || state.closing_) return E_ABORT;
    }
    const bool committed = positions.commit();
    if (!committed) {
        state.sidebar.set_rect(sidebar_list_rect, GetDpiForWindow(window));
    }
    // The tab strip's custom add/scroll-button geometry depends on its new
    // client width. Recompute only after the app batch has committed; before
    // then GetClientRect still describes the previous live-resize frame.
    const std::size_t visible_panes =
        panedock::core::pane_count(group.layout_template);
    for (std::size_t index = 0; index < state.panes.size(); ++index) {
        if (index < visible_panes &&
            (changed_panes[index] || recompute_content))
            apply_tab_item_size(state, index);
    }
    for (std::size_t index = 0; index < state.panes.size(); ++index) {
        if (!explorer_positions[index].has_value()) continue;
        const bool explorer_committed = explorer_positions[index]->commit();
        if (!explorer_committed && shell_positions_deferred[index]) {
            ShellCallScope shell_call(state);
            state.panes[index].host().set_rect(
                *pending_shell_rects[index], nullptr);
            if (state.shutdown_deferred || state.closing_) return E_ABORT;
        }
    }
    for (std::size_t index = 0; index < state.panes.size(); ++index) {
        if (changed_panes[index]) {
            apply_pane_container_region(
                state.panes[index].explorer_container(),
                container_rects[index].right - container_rects[index].left,
                container_rects[index].bottom - container_rects[index].top,
                container_radius);
            RedrawWindow(state.panes[index].explorer_container(),
                         nullptr, nullptr,
                         RDW_INVALIDATE | RDW_NOERASE | RDW_ALLCHILDREN);
        }
        if (index >= panedock::core::pane_count(group.layout_template))
            state.panes[index].laid_out_pane_rect().reset();
    }
    InvalidateRect(window, nullptr, FALSE);
    if (recompute_content) write_live_view_count(state.diagnostic_mode);
    return first_failure.has_value() ? first_failure->result : S_OK;
}

void refresh_startup_chrome(AppState& state) {
    ShellCallScope shell_call(state);
    for (std::size_t index = 0; index < kPinnedFixedParsingNames.size();
         ++index) {
        if (state.shutdown_deferred || state.closing_) break;
        state.pinned_fixed_labels[index] = display_text_for_parsing_name(
            state, kPinnedFixedParsingNames[index]);
    }
    if (!state.shutdown_deferred && !state.closing_)
        refresh_tab_strips(state);
}

HRESULT realize_startup_panes(HWND window, AppState& state) {
    // startup_realize_pending keeps the first pass limited to the active pane.
    refresh_startup_chrome(state);
    if (state.shutdown_deferred || state.closing_) return E_ABORT;
    const HRESULT active_result = apply_layout(window, state);
    if (state.shutdown_deferred || state.closing_) return E_ABORT;
    if (has_active_group(state)) {
        const std::size_t active = active_pane_index(active_group(state));
        {
            ShellCallScope shell_call(state);
            state.panes[active].host().focus();
        }
        if (state.shutdown_deferred || state.closing_) return E_ABORT;
    }

    state.startup_realize_pending = false;
    const HRESULT remaining_result = apply_layout(window, state, true);
    if (state.shutdown_deferred || state.closing_) return E_ABORT;
    if (FAILED(active_result)) return active_result;
    return remaining_result;
}

std::string unique_group_id(const panedock::core::ApplicationState& application) {
    std::size_t maximum = 0;
    bool malformed_numeric_id = false;
    for (const auto& group : application.groups) {
        constexpr std::string_view prefix = "group-";
        if (!group.id.starts_with(prefix)) continue;
        std::size_t value = 0;
        const std::string_view suffix(group.id.data() + prefix.size(),
                                      group.id.size() - prefix.size());
        const auto parsed = std::from_chars(suffix.data(),
                                            suffix.data() + suffix.size(), value);
        if (suffix.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != suffix.data() + suffix.size()) {
            malformed_numeric_id = true;
            break;
        }
        maximum = std::max(maximum, value);
    }
    if (!malformed_numeric_id) return "group-" + std::to_string(maximum + 1);

    const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    std::string candidate = "group-" + std::to_string(ticks);
    std::size_t discriminator = 0;
    while (std::any_of(application.groups.begin(), application.groups.end(),
                       [&](const auto& group) { return group.id == candidate; })) {
        candidate = "group-" + std::to_string(ticks) + "-" +
                    std::to_string(++discriminator);
    }
    return candidate;
}

panedock::core::GroupState new_group_state(const AppState& state,
                                            std::string id) {
    auto group = default_application_state().groups.front();
    group.id = std::move(id);
    group.name = L"Group " +
                 std::to_wstring(state.application.groups.size() + 1);
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

void activate_group(HWND window, AppState& state, std::size_t index) {
    if (state.closing_ || state.shutdown_deferred)
        return;  // re-dispatched during a Shell call or teardown pump
    if (index >= state.application.groups.size()) return;
    const std::string target_id = state.application.groups[index].id;
    if (target_id == state.application.active_group_id) {
        rebind_panes(state);
        refresh_sidebar(state);
        return;
    }

    capture_locations(state);
    state.application.active_group_id = target_id;
    rebind_panes(state);
    refresh_tab_strips(state);
    auto& group = active_group(state);
    if (!navigate_realized_panes(state, group)) return;
    if (FAILED(apply_layout(window, state)))
        OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
    if (state.shutdown_deferred || state.closing_) return;
    {
        ShellCallScope shell_call(state);
        state.panes[active_pane_index(group)].host().focus();
    }
    if (state.shutdown_deferred || state.closing_) return;
    refresh_sidebar(state);
    // Group switches can be triggered from the OLE drag-hover message. Keep
    // the session flush out of that interaction path; shutdown still forces
    // the dirty state through a synchronous save.
    schedule_session_save(state);
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
    if (state.closing_ || state.shutdown_deferred) return;
    const bool was_empty = state.application.groups.empty();
    const std::string id = unique_group_id(state.application);
    if (!panedock::core::add_group(state.application,
                                   new_group_state(state, id))) return;
    if (was_empty) {
        // The first Group is already active when core adds it; route through
        // activate_group's same-id path so binding still has one owner.
        activate_group(window, state, state.application.groups.size() - 1);
        refresh_tab_strips(state);
        if (FAILED(apply_layout(window, state)))
            OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
        if (state.shutdown_deferred || state.closing_) return;
        {
            ShellCallScope shell_call(state);
            state.panes[active_pane_index(active_group(state))].host().focus();
        }
        if (state.shutdown_deferred || state.closing_) return;
        refresh_sidebar(state);
        schedule_session_save(state);
        return;
    }
    activate_group(window, state, state.application.groups.size() - 1);
}

void duplicate_group(HWND window, AppState& state) {
    if (state.closing_ || state.shutdown_deferred) return;
    const auto selected = state.sidebar.selected_index();
    if (!selected.has_value() || *selected >= state.application.groups.size()) return;
    const auto& source = state.application.groups[*selected];
    const std::string id = unique_group_id(state.application);
    if (!panedock::core::duplicate_group(state.application, source.id, id,
                                         source.name + L" copy")) return;
    activate_group(window, state, state.application.groups.size() - 1);
}

void delete_group(HWND window, AppState& state) {
    const auto selected = state.sidebar.selected_index();
    if (!selected.has_value() || *selected >= state.application.groups.size()) return;
    if (MessageBoxW(window,
                    L"Delete this Group? This action cannot be undone.",
                    L"Delete Group", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    if (state.closing_ || state.shutdown_deferred) return;

    capture_locations(state);
    const std::string id = state.application.groups[*selected].id;
    const bool deleted_active = id == state.application.active_group_id;
    // core::delete_group destroys PaneState objects. Drop every borrowed
    // pointer before that erase; rebind only after the surviving Group is known.
    for (auto& pane : state.panes) pane.unbind();
    if (!panedock::core::delete_group(state.application, id)) return;
    rebind_panes(state);
    refresh_tab_strips(state);
    if (deleted_active && has_active_group(state)) {
        auto& group = active_group(state);
        if (!navigate_realized_panes(state, group)) return;
    }
    if (state.shutdown_deferred || state.closing_) return;
    if (FAILED(apply_layout(window, state)))
        OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
    if (state.shutdown_deferred || state.closing_) return;
    if (has_active_group(state)) {
        const std::size_t active = active_pane_index(active_group(state));
        ShellCallScope shell_call(state);
        state.panes[active].host().focus();
    }
    if (state.shutdown_deferred || state.closing_) return;
    refresh_sidebar(state);
    schedule_session_save(state);
}

// No rebind: PD-184 guarantees group reorder preserves PaneState addresses.
void move_group(AppState& state, bool down) {
    const auto selected = state.sidebar.selected_index();
    if (!selected.has_value() || *selected >= state.application.groups.size()) return;
    if ((!down && *selected == 0) ||
        (down && *selected + 1 >= state.application.groups.size())) return;
    const std::string id = state.application.groups[*selected].id;
    const std::size_t target = down ? *selected + 1 : *selected - 1;
    if (!panedock::core::reorder_group(state.application, id, target)) return;
    refresh_sidebar(state);
    schedule_session_save(state);
}

void set_active_pane(HWND window, AppState& state, std::size_t pane) noexcept {
    if (state.closing_ || state.shutdown_deferred) return;
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    if (pane >= group.panes.size()) return;
    const std::size_t previous = active_pane_index(group);
    if (previous == pane ||
        !panedock::core::set_active_pane(group, group.panes[pane].id)) return;
    {
        ShellCallScope shell_call(state);
        state.panes[pane].host().focus();
    }
    if (state.shutdown_deferred || state.closing_) return;
    InvalidateRect(state.panes[previous].tab_strip(), nullptr, FALSE);
    InvalidateRect(state.panes[pane].tab_strip(), nullptr, FALSE);
    InvalidateRect(window, nullptr, TRUE);
    schedule_session_save(state);
}

std::string unique_tab_id(const panedock::core::GroupState& group,
                          std::size_t& candidate_index) {
    for (;;) {
        const std::string candidate =
            "tab-" + std::to_string(candidate_index++);
        const bool exists = std::any_of(
            group.panes.begin(), group.panes.end(), [&](const auto& pane) {
                return std::any_of(
                    pane.tabs.begin(), pane.tabs.end(), [&](const auto& tab) {
                        return tab.id == candidate;
                    });
            });
        if (!exists) return candidate;
    }
}

void switch_active_tab(HWND, AppState& state, std::size_t pane_index,
                       const std::string& tab_id) {
    if (state.closing_ || state.shutdown_deferred)
        return;  // re-dispatched during a Shell call or teardown pump
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    if (pane_index >= group.panes.size()) return;
    auto& pane = group.panes[pane_index];
    if (pane.active_tab_id == tab_id) {
        refresh_tab_strip(state, pane_index);
        return;
    }
    capture_pane_location(state, pane_index);
    if (!panedock::core::set_active_tab(pane, tab_id)) {
        refresh_tab_strip(state, pane_index);
        return;
    }
    if (state.panes[pane_index].realized()) {
        {
            ShellCallScope shell_call(state);
            navigate_pane(
                state, pane_index,
                active_tab(pane).location);
        }
        if (state.shutdown_deferred || state.closing_) return;
    }
    refresh_tab_strip(state, pane_index);
    schedule_session_save(state);
}

std::optional<std::size_t> tab_item_at_point(const AppState& state, HWND strip,
                                             POINT point) noexcept;

bool register_tab_drag_hover_targets(HWND window, AppState& state) {
    for (std::size_t pane_index = 0;
         pane_index < state.panes.size(); ++pane_index) {
        const HWND strip = state.panes[pane_index].tab_strip();
        auto hit_test = [&state, strip,
                         pane_index](POINT screen) -> std::optional<std::size_t> {
            if (!has_active_group(state) ||
                pane_index >= active_group(state).panes.size())
                return std::nullopt;
            POINT client = screen;
            ScreenToClient(strip, &client);
            return tab_item_at_point(state, strip, client);
        };
        auto hover_callback = [&state, window,
                               pane_index](std::size_t item) {
            if (!has_active_group(state) ||
                pane_index >= active_group(state).panes.size())
                return;
            auto& tabs = active_group(state).panes[pane_index].tabs;
            if (item >= tabs.size()) return;
            switch_active_tab(window, state, pane_index, tabs[item].id);
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
            ShellCallScope shell_call(state);
            registered =
                state.panes[pane_index].register_drag_hover_target(target.Get());
        }
        if (state.shutdown_deferred || state.closing_ || !registered)
            return false;
    }
    return true;
}

void cycle_active_tab(HWND window, AppState& state, std::size_t pane_index,
                      bool reverse) {
    auto& pane = active_group(state).panes[pane_index];
    const auto current = std::find_if(
        pane.tabs.begin(), pane.tabs.end(), [&](const auto& tab) {
            return tab.id == pane.active_tab_id;
        });
    if (current == pane.tabs.end()) return;
    const std::size_t index =
        static_cast<std::size_t>(current - pane.tabs.begin());
    const std::size_t next = reverse ? (index + pane.tabs.size() - 1) %
                                          pane.tabs.size()
                                     : (index + 1) % pane.tabs.size();
    switch_active_tab(window, state, pane_index, pane.tabs[next].id);
}

void add_tab_to_pane(
    HWND, AppState& state, std::size_t pane_index,
    panedock::core::ShellLocation initial_location = default_shell_location()) {
    if (state.closing_ || state.shutdown_deferred) return;
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    if (pane_index >= group.panes.size()) return;
    capture_pane_location(state, pane_index);
    std::size_t candidate = 0;
    const std::string id = unique_tab_id(group, candidate);
    auto& pane = group.panes[pane_index];
    if (!panedock::core::add_tab(
            pane, {id, std::move(initial_location), {}, {}, true,
                   {}, 0}) ||
        !panedock::core::set_active_tab(pane, id)) return;
    if (state.panes[pane_index].realized()) {
        {
            ShellCallScope shell_call(state);
            navigate_pane(
                state, pane_index,
                active_tab(pane).location);
        }
        if (state.shutdown_deferred || state.closing_) return;
    }
    refresh_tab_strip(state, pane_index);
    schedule_session_save(state);
}

void close_tab_in_pane(HWND, AppState& state, std::size_t pane_index,
                       const std::string& tab_id) {
    if (state.closing_ || state.shutdown_deferred) return;
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    if (pane_index >= group.panes.size()) return;
    auto& pane = group.panes[pane_index];
    capture_pane_location(state, pane_index);
    const bool closed_active = pane.active_tab_id == tab_id;
    if (!panedock::core::close_tab(
            pane, tab_id, default_shell_location())) return;
    if (closed_active && state.panes[pane_index].realized()) {
        {
            ShellCallScope shell_call(state);
            navigate_pane(
                state, pane_index,
                active_tab(pane).location);
        }
        if (state.shutdown_deferred || state.closing_) return;
    }
    refresh_tab_strip(state, pane_index);
    schedule_session_save(state);
}

void navigate_tab_history(AppState& state, std::size_t pane_index, bool back) {
    if (state.closing_ || state.shutdown_deferred) return;
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size() ||
        state.panes[pane_index].suppress_history()) return;
    auto& tab = active_tab(active_group(state).panes[pane_index]);
    const bool moved = back ? panedock::core::navigate_tab_back(tab)
                            : panedock::core::navigate_tab_forward(tab);
    if (!moved) return;
    state.panes[pane_index].set_suppress_history(true);
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCallScope shell_call(state);
        hr = navigate_pane(state, pane_index, tab.location);
    }
    if (state.shutdown_deferred || state.closing_) return;
    if (FAILED(hr)) state.panes[pane_index].set_suppress_history(false);
    refresh_navigation_chrome(state, pane_index);
}

void navigate_up(AppState& state, std::size_t pane_index) {
    if (state.closing_ || state.shutdown_deferred) return;
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size()) return;
    {
        ShellCallScope shell_call(state);
        (void)navigate_up_pane(state, pane_index);
    }
}

void refresh_pane(AppState& state, std::size_t pane_index) {
    if (state.closing_ || state.shutdown_deferred) return;
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size())
        return;
    {
        ShellCallScope shell_call(state);
        (void)state.panes[pane_index].host().refresh(
            begin_navigation(state, pane_index));
    }
}

void set_pane_view_mode(AppState& state, std::size_t pane_index,
                        const ViewModeOption& option) {
    if (state.closing_ || state.shutdown_deferred) return;
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size())
        return;
    HRESULT hr = E_UNEXPECTED;
    {
        ShellCallScope shell_call(state);
        hr = state.panes[pane_index].host().set_view_mode(option.selection.mode,
                                                        option.selection.image_size);
    }
    if (state.shutdown_deferred || state.closing_) return;
    if (SUCCEEDED(hr)) {
        active_tab(active_group(state).panes[pane_index]).view_mode =
            panedock::shell_core::view_mode_name(option.selection.mode,
                                                 option.selection.image_size);
        schedule_session_save(state);
    }
}

void show_view_mode_menu(HWND window, AppState& state,
                         std::size_t pane_index) {
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size())
        return;

    RECT button_rect{};
    if (!GetWindowRect(state.panes[pane_index].view_mode_button(),
                       &button_rect))
        return;

    const auto current = panedock::shell_core::parse_view_mode(
        active_tab(active_group(state).panes[pane_index]).view_mode);
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    const int menu_id_base =
        kViewModeMenuIdBase +
        static_cast<int>(pane_index * kViewModeOptions.size());
    int checked_id = 0;
    for (std::size_t index = 0; index < kViewModeOptions.size(); ++index) {
        const bool checked = current.has_value() &&
                             current->mode ==
                                     kViewModeOptions[index].selection.mode &&
                             current->image_size ==
                                 kViewModeOptions[index].selection.image_size;
        const int id = menu_id_base + static_cast<int>(index);
        AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(id), kViewModeOptions[index].label);
        if (checked) checked_id = id;
    }
    if (checked_id != 0) {
        CheckMenuRadioItem(menu, menu_id_base,
                           menu_id_base +
                               static_cast<int>(kViewModeOptions.size()) - 1,
                           checked_id, MF_BYCOMMAND);
    }
    SetForegroundWindow(window);
    const int command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, button_rect.left,
        button_rect.bottom, 0, window, nullptr);
    DestroyMenu(menu);
    if (state.closing_ || state.shutdown_deferred) return;
    if (command != 0)
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(command, 0), 0);
}

void add_current_folder(AppState& state, std::size_t pane_index) {
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size() ||
        state.application.pinned_locations.size() >=
            static_cast<std::size_t>(kPinnedMenuMaxLocationCount))
        return;
    capture_pane_location(state, pane_index);
    const auto current_location =
        active_tab(active_group(state).panes[pane_index]).location;
    if (panedock::core::add_pinned_location(state.application,
                                            current_location)) {
        schedule_session_save(state);
        if (state.pinned_locations_dialog.is_open())
            state.pinned_locations_dialog.add_location(
                current_location,
                display_text_for_parsing_name(state,
                                              current_location.parsing_name));
    }
}

void show_pinned_locations_menu(HWND window, AppState& state,
                               std::size_t pane_index) {
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size())
        return;

    RECT button_rect{};
    if (!GetWindowRect(state.panes[pane_index].pinned_button(),
                       &button_rect))
        return;

    const int menu_id_base =
        kPinnedMenuIdBase +
        static_cast<int>(pane_index * kPinnedMenuSlotsPerPane);
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    AppendMenuW(menu, MF_STRING,
                static_cast<UINT_PTR>(menu_id_base + kPinnedMenuDesktopOffset),
                state.pinned_fixed_labels[0].c_str());
    AppendMenuW(menu, MF_STRING,
                static_cast<UINT_PTR>(menu_id_base + kPinnedMenuThisPcOffset),
                state.pinned_fixed_labels[1].c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const std::size_t count = std::min(
        state.application.pinned_locations.size(),
        static_cast<std::size_t>(kPinnedMenuMaxLocationCount));
    for (std::size_t index = 0; index < count; ++index) {
        const std::wstring label = display_text_for_parsing_name(
            state, state.application.pinned_locations[index].parsing_name);
        AppendMenuW(
            menu, MF_STRING,
            static_cast<UINT_PTR>(menu_id_base + kPinnedMenuLocationOffset +
                                  static_cast<int>(index)),
            label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING,
                static_cast<UINT_PTR>(menu_id_base + kPinnedMenuAddOffset),
                L"Add Current Folder");
    AppendMenuW(menu, MF_STRING,
                static_cast<UINT_PTR>(menu_id_base + kPinnedMenuManageOffset),
                L"Manage Pinned Locations...");
    SetForegroundWindow(window);
    const int command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, button_rect.left,
        button_rect.bottom, 0, window, nullptr);
    DestroyMenu(menu);
    if (state.closing_ || state.shutdown_deferred) return;
    if (command != 0)
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(command, 0), 0);
}

void submit_address(AppState& state, std::size_t pane_index) {
    if (state.closing_ || state.shutdown_deferred) return;
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size()) return;
    const HWND edit = state.panes[pane_index].address_bar();
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(edit, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    {
        ShellCallScope shell_call(state);
        (void)navigate_pane(state, pane_index, location(std::move(text)));
    }
}

LRESULT CALLBACK address_edit_proc(HWND window, UINT message, WPARAM wparam,
                                   LPARAM lparam, UINT_PTR pane_index,
                                   DWORD_PTR reference_data) {
    auto* state = reinterpret_cast<AppState*>(reference_data);
    if (state != nullptr && (state->closing_ || state->shutdown_deferred) &&
        message != WM_NCDESTROY)
        return 0;
    if (message == WM_KEYDOWN && wparam == VK_RETURN && state != nullptr) {
        submit_address(*state, static_cast<std::size_t>(pane_index));
        return 0;
    }
    if (message == WM_CHAR && wparam == VK_RETURN) return 0;
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, address_edit_proc, pane_index);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

void set_layout(HWND window, AppState& state,
                panedock::core::LayoutTemplate target) noexcept {
    if (state.closing_ || state.shutdown_deferred) return;
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    capture_locations(state);

    std::vector<std::string> pane_ids;
    std::vector<std::string> tab_ids;
    const std::size_t target_count = panedock::core::pane_count(target);
    std::size_t tab_candidate = 0;
    for (std::size_t index = group.panes.size(); index < target_count; ++index) {
        pane_ids.push_back("pane-" + std::to_string(index));
        tab_ids.push_back(unique_tab_id(group, tab_candidate));
    }
    if (!panedock::core::switch_layout(group, target,
                                       default_shell_location(),
                                       pane_ids, tab_ids)) return;
    rebind_panes(state);
    refresh_tab_strips(state);
    if (FAILED(apply_layout(window, state))) {
        OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
    }
    if (state.shutdown_deferred || state.closing_) return;

    const std::size_t active = active_pane_index(group);
    {
        ShellCallScope shell_call(state);
        state.panes[active].host().focus();
    }
    if (state.shutdown_deferred || state.closing_) return;
    schedule_session_save(state);
}

void update_sidebar_drag(HWND window, AppState& state, POINT point,
                         bool recompute_content = false) {
    if (!state.sidebar_drag.has_value()) return;
    const int dpi = std::max(1, static_cast<int>(GetDpiForWindow(window)));
    const int delta = MulDiv(
        point.x - state.sidebar_drag->start_x, 96, dpi);
    state.application.sidebar_width = std::clamp(
        state.sidebar_drag->start_width + delta, kSidebarMinimumWidth,
        kSidebarMaximumWidth);
    if (FAILED(apply_layout(window, state, false, recompute_content)))
        OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
}

void update_splitter_drag(HWND window, AppState& state, POINT point,
                          bool recompute_content = false) {
    if (!state.splitter_drag.has_value()) return;
    auto& group = active_group(state);
    const Splitter& drag = *state.splitter_drag;
    if (drag.ratio_index >= group.divider_ratios.size()) return;

    const RECT client = pane_content_area(window, state);
    const int size = drag.vertical ? client.right - client.left
                                   : client.bottom - client.top;
    const int divider = layout_metrics(window).divider_thickness;
    const int available = std::max(size, divider) - divider;
    if (available <= 0) return;
    const int position = drag.vertical ? point.x - client.left
                                       : point.y - client.top;
    group.divider_ratios[drag.ratio_index] = std::clamp(
        static_cast<double>(position) / static_cast<double>(available),
        0.0, 1.0);
    if (FAILED(apply_layout(window, state, false, recompute_content)))
        OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
}

std::size_t pane_at_point(HWND window, const AppState& state,
                          POINT point) noexcept {
    if (!has_active_group(state)) return kExplorerCount;
    const auto rects = layout_rects(window, state, active_group(state));
    for (std::size_t index = 0; index < rects.size(); ++index) {
        const RECT rect = to_win32_rect(rects[index]);
        if (PtInRect(&rect, point)) return index;
    }
    return kExplorerCount;
}

void capture_window_placement(HWND window, AppState& state) noexcept {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (!GetWindowPlacement(window, &placement)) {
        OutputDebugStringW(L"PaneDock: GetWindowPlacement failed\n");
        return;
    }
    const RECT& normal = placement.rcNormalPosition;
    state.application.window_placement = {
        normal.left, normal.top, normal.right - normal.left,
        normal.bottom - normal.top, placement.showCmd == SW_SHOWMAXIMIZED};
}

const wchar_t* session_source_name(panedock::core::SessionSource source) {
    switch (source) {
        case panedock::core::SessionSource::primary: return L"primary";
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

std::optional<std::size_t> tab_strip_index(const AppState& state,
                                            HWND strip) noexcept {
    for (std::size_t index = 0; index < state.panes.size(); ++index)
        if (state.panes[index].tab_strip() == strip) return index;
    return std::nullopt;
}

bool address_bar_has_focus(const AppState& state) noexcept {
    const HWND focused = GetFocus();
    for (const auto& chrome : state.panes)
        if (chrome.address_bar() == focused) return true;
    return false;
}

void close_tab_at_point(HWND window, AppState& state, POINT point) {
    const std::size_t pane_index = pane_at_point(window, state, point);
    if (pane_index >= state.panes.size() ||
        pane_index >= active_group(state).panes.size()) return;
    POINT client = point;
    const HWND strip = state.panes[pane_index].tab_strip();
    MapWindowPoints(window, strip, &client, 1);
    const auto item = tab_item_at_point(
        state, strip, client);
    const auto& tabs = active_group(state).panes[pane_index].tabs;
    if (!item.has_value() || *item >= tabs.size()) return;
    const std::string id = tabs[*item].id;
    close_tab_in_pane(window, state, pane_index, id);
}

RECT tab_viewport_rect(const AppState& state, std::size_t pane_index) noexcept {
    return to_win32_rect(state.panes[pane_index].tab_geometry().viewport);
}

std::optional<std::size_t> tab_item_at_point(const AppState& state, HWND strip,
                                             POINT point) noexcept {
    const auto pane_index = tab_strip_index(state, strip);
    if (!pane_index.has_value() || !has_active_group(state) ||
        *pane_index >= active_group(state).panes.size()) {
        return std::nullopt;
    }
    return state.panes[*pane_index].tab_at(point);
}

std::optional<std::size_t> tab_scroll_button_at_point(
    const AppState& state, std::size_t pane_index, POINT point) noexcept {
    if (pane_index >= state.panes.size())
        return std::nullopt;
    return panedock::app_shell::tab_scroll_button_hit_test(
        state.panes[pane_index].tab_geometry(), point.x, point.y);
}

int tab_scroll_step(const AppState& state, std::size_t pane_index,
                    bool forward) noexcept {
    if (pane_index >= state.panes.size()) return 0;
    return panedock::app_shell::tab_scroll_step(
        state.panes[pane_index].tab_geometry(), forward);
}

void scroll_tab_strip(AppState& state, std::size_t pane_index, bool forward) {
    if (pane_index >= state.panes.size()) return;
    auto& chrome = state.panes[pane_index];
    panedock::app_shell::TabStripGeometry geometry = chrome.tab_geometry();
    if (geometry.max_scroll_offset <= 0) return;
    const int offset = geometry.scroll_offset;
    const int maximum = geometry.max_scroll_offset;
    if ((!forward && offset <= 0) || (forward && offset >= maximum)) return;
    const int step = tab_scroll_step(state, pane_index, forward);
    if (step <= 0) return;
    geometry.scroll_offset = offset + (forward ? step : -step);
    chrome.set_geometry(std::move(geometry));
    apply_tab_item_size(state, pane_index);
}

void cancel_tab_drag(AppState& state, HWND strip) noexcept {
    if (!state.tab_drag.has_value() || state.tab_drag->strip != strip)
        return;
    const std::size_t source = state.tab_drag->pane_index;
    const auto target = state.tab_drag->target_pane_index;
    state.tab_drag.reset();
    apply_tab_item_size(state, source);
    if (target.has_value() && *target != source)
        apply_tab_item_size(state, *target);
    if (GetCapture() == strip) ReleaseCapture();
}

void finish_tab_drag(AppState& state, HWND strip) {
    if (state.closing_ || state.shutdown_deferred) return;
    if (!state.tab_drag.has_value() || state.tab_drag->strip != strip)
        return;
    AppState::TabDrag drag = std::move(*state.tab_drag);
    state.tab_drag.reset();
    apply_tab_item_size(state, drag.pane_index);
    if (drag.target_pane_index.has_value())
        apply_tab_item_size(state, *drag.target_pane_index);
    if (GetCapture() == strip) ReleaseCapture();
    if (!drag.dragging || !drag.target_index.has_value() ||
        !has_active_group(state))
        return;
    auto& group = active_group(state);
    if (drag.pane_index >= group.panes.size()) return;
    const std::size_t target_pane =
        drag.target_pane_index.value_or(drag.pane_index);
    if (target_pane >= group.panes.size()) return;
    if (target_pane == drag.pane_index) {
        if (*drag.target_index == drag.source_index) return;
        if (!panedock::core::reorder_tab(group.panes[drag.pane_index],
                                         drag.tab_id, *drag.target_index))
            return;
        refresh_tab_strip(state, drag.pane_index);
        schedule_session_save(state);
        return;
    }

    capture_pane_location(state, drag.pane_index);
    capture_pane_location(state, target_pane);
    auto& source = group.panes[drag.pane_index];
    auto& target = group.panes[target_pane];
    const bool source_active = source.active_tab_id == drag.tab_id;
    if (!panedock::core::move_tab(
            source, target, drag.tab_id, *drag.target_index,
            default_shell_location())) return;
    if (source_active && state.panes[drag.pane_index].realized()) {
        {
            ShellCallScope shell_call(state);
            navigate_pane(
                state, drag.pane_index,
                active_tab(source).location);
        }
        if (state.shutdown_deferred || state.closing_) return;
    }
    if (state.panes[target_pane].realized()) {
        {
            ShellCallScope shell_call(state);
            navigate_pane(
                state, target_pane,
                active_tab(target).location);
        }
        if (state.shutdown_deferred || state.closing_) return;
    }
    refresh_tab_strip(state, drag.pane_index);
    refresh_tab_strip(state, target_pane);
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
    if (!state.tab_drag->dragging) {
        const int threshold = std::max(
            1, std::max(GetSystemMetrics(SM_CXDRAG),
                        GetSystemMetrics(SM_CYDRAG)));
        const int dx = point.x - state.tab_drag->start.x;
        const int dy = point.y - state.tab_drag->start.y;
        if (dx < -threshold || dx > threshold || dy < -threshold ||
            dy > threshold) {
            state.tab_drag->dragging = true;
        }
    }
    if (!state.tab_drag->dragging) return;

    POINT screen = point;
    ClientToScreen(strip, &screen);
    std::optional<std::size_t> target_pane;
    std::optional<std::size_t> target;
    const std::size_t pane_count = has_active_group(state)
                                       ? active_group(state).panes.size()
                                       : 0;
    for (std::size_t index = 0; index < pane_count; ++index) {
        const HWND candidate = state.panes[index].tab_strip();
        if (!IsWindowVisible(candidate)) continue;
        POINT client_point = screen;
        ScreenToClient(candidate, &client_point);
        RECT client{};
        GetClientRect(candidate, &client);
        if (!PtInRect(&client, client_point)) continue;
        target_pane = index == state.tab_drag->pane_index
                          ? std::nullopt
                          : std::optional<std::size_t>{index};
        target = state.panes[index].tab_at_screen(screen);
        const RECT viewport =
            to_win32_rect(state.panes[index].tab_geometry().viewport);
        if (!target.has_value() && index != state.tab_drag->pane_index &&
            PtInRect(&viewport, client_point)) {
            target = active_group(state).panes[index].tabs.size();
        }
        break;
    }
    if (target_pane == state.tab_drag->target_pane_index &&
        target == state.tab_drag->target_index) return;
    const auto previous_target = state.tab_drag->target_pane_index;
    state.tab_drag->target_pane_index = target_pane;
    state.tab_drag->target_index = target;
    apply_tab_item_size(state, state.tab_drag->pane_index);
    if (previous_target.has_value() &&
        previous_target != state.tab_drag->target_pane_index)
        apply_tab_item_size(state, *previous_target);
    if (state.tab_drag->target_pane_index.has_value())
        apply_tab_item_size(state, *state.tab_drag->target_pane_index);
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

void paint_tab_strip(HWND window, AppState& state, std::size_t pane_index,
                     HDC dc) noexcept {
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size()) return;
    const auto& pane = active_group(state).panes[pane_index];
    const auto& visuals = state.panes[pane_index].tab_visuals();
    const int horizontal_gap = scaled_value(window, kTabHorizontalGap);
    const int left_gap = horizontal_gap / 2;
    const int right_gap = horizontal_gap - left_gap;
    const int text_padding =
        scaled_value(window, kTabTextHorizontalPadding);
    const int vertical_padding = scaled_value(window, kTabVerticalPadding);
    const int radius = scaled_value(window, kTabCornerRadius);
    const int border_width = scaled_value(window, 1);
    const HGDIOBJ old_font = state.chrome_font != nullptr
                                 ? SelectObject(dc, state.chrome_font)
                                 : nullptr;
    SetBkMode(dc, TRANSPARENT);
    const RECT viewport = tab_viewport_rect(state, pane_index);
    const int saved_dc = SaveDC(dc);
    IntersectClipRect(dc, viewport.left, viewport.top, viewport.right,
                      viewport.bottom);
    for (std::size_t index = 0; index < visuals.size(); ++index) {
        if (state.tab_drag.has_value() && state.tab_drag->dragging &&
            state.tab_drag->pane_index == pane_index &&
            index == state.tab_drag->source_index) continue;
        RECT rect = to_win32_rect(
            state.panes[pane_index].tab_geometry().tab_rects[index]);
        rect.left = std::min(rect.right, rect.left + left_gap);
        rect.right = std::max(rect.left, rect.right - right_gap);
        rect.top = std::min(rect.bottom, rect.top + vertical_padding);
        rect.bottom = std::max(rect.top, rect.bottom - vertical_padding);
        const bool active = pane.tabs[index].id == pane.active_tab_id;
        const bool hovered = !active &&
                             state.panes[pane_index].tab_hover_index() == index;
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
    const auto& geometry = state.panes[pane_index].tab_geometry();
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
        const std::size_t target_pane =
            state.tab_drag.has_value()
                ? state.tab_drag->target_pane_index.value_or(
                      state.tab_drag->pane_index)
                : pane_index;
        if (state.tab_drag.has_value() && state.tab_drag->dragging &&
            target_pane == pane_index &&
            state.tab_drag->pane_index < active_group(state).panes.size()) {
            const auto& source_pane =
                active_group(state).panes[state.tab_drag->pane_index];
            const auto source_tab = std::find_if(
                source_pane.tabs.begin(), source_pane.tabs.end(),
                [&](const auto& tab) { return tab.id == state.tab_drag->tab_id; });
            if (source_tab != source_pane.tabs.end()) {
                RECT text_rect = rect;
                text_rect.left = std::min(text_rect.right,
                                          text_rect.left + text_padding);
                text_rect.right = std::max(text_rect.left,
                                           text_rect.right - text_padding);
                SetTextColor(dc, panedock::sidebar::kPlaceholderContent);
                const std::wstring text = tab_display_text(state, *source_tab);
                DrawTextW(dc, text.c_str(), -1, &text_rect,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
                              DT_NOPREFIX);
            }
        }
    }
    if (saved_dc != 0) RestoreDC(dc, saved_dc);
    const auto& scroll_buttons = geometry.scroll_button_rects;
    if (scroll_buttons[0].right > scroll_buttons[0].left) {
        draw_tab_scroll_button(
            window, dc, to_win32_rect(scroll_buttons[0]), false,
            geometry.scroll_offset <= 0,
            state.panes[pane_index].scroll_hover_index() == 0);
        draw_tab_scroll_button(
            window, dc, to_win32_rect(scroll_buttons[1]), true,
            geometry.scroll_offset >= geometry.max_scroll_offset,
            state.panes[pane_index].scroll_hover_index() == 1);
    }
    const RECT add = to_win32_rect(geometry.add_rect);
    if (state.panes[pane_index].tab_hover_index().has_value() &&
        *state.panes[pane_index].tab_hover_index() == pane.tabs.size() &&
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
        if (state.chrome_font != nullptr) {
            LOGFONTW logfont{};
            if (GetObjectW(state.chrome_font, sizeof(logfont), &logfont) ==
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
    if (pane_index == active_pane_index(active_group(state))) {
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

LRESULT CALLBACK tab_strip_proc(HWND window, UINT message, WPARAM wparam,
                                LPARAM lparam, UINT_PTR pane_index,
                                DWORD_PTR reference_data) {
    auto* state = reinterpret_cast<AppState*>(reference_data);
    if (state != nullptr && pane_index < state->panes.size()) {
        if ((state->closing_ || state->shutdown_deferred) &&
            message != WM_PAINT && message != WM_ERASEBKGND &&
            message != WM_NCDESTROY)
            return 0;
        if (defer_shell_reentry_mouse_message(window, *state, message,
                                              wparam, lparam))
            return 0;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            paint_tab_strip(window, *state, pane_index, dc);
            EndPaint(window, &paint);
            return 0;
        }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_MOUSEWHEEL && has_active_group(*state) &&
            pane_index < active_group(*state).panes.size()) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            if (delta != 0)
                scroll_tab_strip(*state, pane_index, delta < 0);
            return 0;
        }
        if (message == WM_LBUTTONDOWN && has_active_group(*state) &&
            pane_index < active_group(*state).panes.size()) {
            const POINT point = point_from_lparam(lparam);
            const auto scroll = tab_scroll_button_at_point(
                *state, pane_index, point);
            if (scroll.has_value()) {
                scroll_tab_strip(*state, pane_index, *scroll == 1);
                return 0;
            }
            const auto item = tab_item_at_point(*state, window, point);
            const RECT add = to_win32_rect(
                state->panes[pane_index].tab_geometry().add_rect);
            if (item.has_value()) {
                const auto& tabs = active_group(*state).panes[pane_index].tabs;
                if (*item >= tabs.size()) return 0;
                if (state->tab_drag.has_value())
                    cancel_tab_drag(*state, state->tab_drag->strip);
                state->tab_drag = AppState::TabDrag{
                    window, pane_index, *item, tabs[*item].id, point, false,
                    std::nullopt, std::nullopt};
                SetCapture(window);
                SendMessageW(GetParent(window), kTabStripSelectionMessage,
                             static_cast<WPARAM>(pane_index),
                             static_cast<LPARAM>(*item));
            } else if (PtInRect(&add, point)) {
                SendMessageW(GetParent(window), kTabStripSelectionMessage,
                             static_cast<WPARAM>(pane_index), -1);
            }
            return 0;
        }
        if (message == WM_LBUTTONDBLCLK && has_active_group(*state) &&
            pane_index < active_group(*state).panes.size()) {
            const POINT point = point_from_lparam(lparam);
            const RECT add = to_win32_rect(
                state->panes[pane_index].tab_geometry().add_rect);
            if (!tab_scroll_button_at_point(*state, pane_index, point) &&
                !tab_item_at_point(*state, window, point) &&
                !PtInRect(&add, point)) {
                SendMessageW(GetParent(window), kTabStripSelectionMessage,
                             static_cast<WPARAM>(pane_index), -1);
                return 0;
            }
        }
        if (message == WM_MOUSEMOVE) {
            const POINT point = point_from_lparam(lparam);
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            std::optional<std::size_t> hover =
                tab_item_at_point(*state, window, point);
            const RECT add = to_win32_rect(
                state->panes[pane_index].tab_geometry().add_rect);
            if (!hover.has_value() && has_active_group(*state) &&
                pane_index < active_group(*state).panes.size() &&
                PtInRect(&add, point)) {
                hover = active_group(*state).panes[pane_index].tabs.size();
            }
            auto& chrome = state->panes[pane_index];
            const bool tab_hover_changed = chrome.tab_hover_index() != hover;
            if (tab_hover_changed) chrome.set_tab_hover(hover);
            const auto scroll_hover = tab_scroll_button_at_point(
                *state, pane_index, point);
            const bool scroll_hover_changed =
                chrome.scroll_hover_index() != scroll_hover;
            if (scroll_hover_changed) chrome.set_scroll_hover(scroll_hover);
            if (tab_hover_changed || scroll_hover_changed)
                InvalidateRect(window, nullptr, FALSE);
            update_tab_drag(*state, window, wparam, lparam);
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            auto& chrome = state->panes[pane_index];
            const bool hover_changed = chrome.tab_hover_index().has_value() ||
                                       chrome.scroll_hover_index().has_value();
            chrome.set_tab_hover(std::nullopt);
            chrome.set_scroll_hover(std::nullopt);
            if (hover_changed)
                InvalidateRect(window, nullptr, FALSE);
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
            if (!control.has_value() ||
                control->control !=
                    panedock::app_shell::PaneControl::folder_context)
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

std::optional<std::size_t> group_item_at_point(const AppState& state,
                                               HWND list,
                                               POINT point) noexcept {
    if (list != state.sidebar.window()) return std::nullopt;
    const LRESULT hit = SendMessageW(
        list, LB_ITEMFROMPOINT, 0, MAKELPARAM(point.x, point.y));
    const std::size_t index = static_cast<std::size_t>(LOWORD(hit));
    if (HIWORD(hit) != 0 || index >= state.application.groups.size())
        return std::nullopt;
    return index;
}

void cancel_group_drag(AppState& state, HWND list) noexcept {
    if (!state.group_drag.has_value() || state.group_drag->list != list)
        return;
    state.group_drag.reset();
    InvalidateRect(list, nullptr, FALSE);
    if (GetCapture() == list) ReleaseCapture();
}

void finish_group_drag(AppState& state, HWND list) {
    if (state.closing_ || state.shutdown_deferred) return;
    if (!state.group_drag.has_value() || state.group_drag->list != list)
        return;
    AppState::GroupDrag drag = std::move(*state.group_drag);
    state.group_drag.reset();
    InvalidateRect(list, nullptr, FALSE);
    if (GetCapture() == list) ReleaseCapture();
    if (!drag.dragging || !drag.target_index.has_value() ||
        *drag.target_index == drag.source_index)
        return;
    if (panedock::core::reorder_group(state.application, drag.group_id,
                                      *drag.target_index)) {
        refresh_sidebar(state);
        schedule_session_save(state);
    }
}

void update_group_drag(AppState& state, HWND list, WPARAM wparam,
                       LPARAM lparam) {
    if (!state.group_drag.has_value() || state.group_drag->list != list)
        return;
    if ((wparam & MK_LBUTTON) == 0) {
        cancel_group_drag(state, list);
        return;
    }
    const POINT point = point_from_lparam(lparam);
    RECT client{};
    GetClientRect(list, &client);
    if (!PtInRect(&client, point)) {
        cancel_group_drag(state, list);
        return;
    }
    if (!state.group_drag->dragging) {
        const int threshold = std::max(
            1, std::max(GetSystemMetrics(SM_CXDRAG),
                        GetSystemMetrics(SM_CYDRAG)));
        const int dx = point.x - state.group_drag->start.x;
        const int dy = point.y - state.group_drag->start.y;
        if (dx < -threshold || dx > threshold || dy < -threshold ||
            dy > threshold) {
            state.group_drag->dragging = true;
            // PD-057: take the capture only now. Before this point the
            // gesture is still an ordinary click and the LISTBOX must keep
            // its own capture, or it cancels the selection instead of
            // reporting it.
            if (GetCapture() != list) SetCapture(list);
        }
    }
    if (!state.group_drag->dragging) return;
    const auto target = group_item_at_point(state, list, point);
    if (target == state.group_drag->target_index) return;
    state.group_drag->target_index = target;
    InvalidateRect(list, nullptr, FALSE);
}

LRESULT CALLBACK group_list_proc(HWND window, UINT message, WPARAM wparam,
                                 LPARAM lparam, UINT_PTR,
                                 DWORD_PTR reference_data) {
    auto* state = reinterpret_cast<AppState*>(reference_data);
    if (state != nullptr && (state->closing_ || state->shutdown_deferred) &&
        message != WM_PAINT && message != WM_ERASEBKGND &&
        message != WM_NCDESTROY)
        return 0;
    if (state != nullptr &&
        defer_shell_reentry_mouse_message(window, *state, message, wparam,
                                          lparam))
        return 0;
    if (message == WM_LBUTTONDOWN && state != nullptr) {
        const POINT point = point_from_lparam(lparam);
        std::optional<AppState::GroupDrag> pending;
        const auto item = group_item_at_point(*state, window, point);
        if (item.has_value()) {
            pending = AppState::GroupDrag{
                window, *item, state->application.groups[*item].id, point,
                false, std::nullopt};
        }
        const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
        if (pending.has_value() && !state->group_drag.has_value()) {
            // PD-057: record the potential drag but do NOT take the capture
            // yet. The LISTBOX runs its own capture-based click tracking
            // between button-down and button-up; interfering with it makes
            // the control report LBN_SELCANCEL instead of LBN_SELCHANGE, so
            // the Group never switches. The capture is taken in
            // update_group_drag once the drag threshold is actually crossed,
            // by which point the click is no longer a plain selection.
            state->group_drag = std::move(*pending);
        }
        return result;
    }
    if (message == WM_MOUSEMOVE && state != nullptr) {
        const POINT point = point_from_lparam(lparam);
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
        state->sidebar.set_hover_index(
            group_item_at_point(*state, window, point));
        update_group_drag(*state, window, wparam, lparam);
        if (state->group_drag.has_value() &&
            state->group_drag->list == window &&
            state->group_drag->dragging)
            return 0;
    } else if (message == WM_MOUSELEAVE && state != nullptr) {
        state->sidebar.set_hover_index(std::nullopt);
        return 0;
    } else if (message == WM_LBUTTONUP && state != nullptr) {
        const bool dragging = state->group_drag.has_value() &&
                              state->group_drag->list == window &&
                              state->group_drag->dragging;
        // PD-057: a plain click must reach the LISTBOX first. Its selection
        // is committed — and LBN_SELCHANGE sent — while it processes
        // WM_LBUTTONUP, and finish_group_drag releases the capture the
        // control is still relying on. Releasing first makes the LISTBOX
        // abandon the click via WM_CAPTURECHANGED, so the notification never
        // arrives and Groups cannot be switched. While actually dragging we
        // still swallow the message, or ending a reorder would also switch
        // the Group under the cursor. Mirrors the WM_LBUTTONDOWN branch,
        // which already defers to the control before touching our state.
        if (!dragging) {
            const LRESULT result =
                DefSubclassProc(window, message, wparam, lparam);
            finish_group_drag(*state, window);
            return result;
        }
        finish_group_drag(*state, window);
        return 0;
    } else if (message == WM_CAPTURECHANGED && state != nullptr) {
        cancel_group_drag(*state, window);
    } else if (message == WM_NCDESTROY && state != nullptr) {
        cancel_group_drag(*state, window);
        RemoveWindowSubclass(window, group_list_proc, 0);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

void begin_shutdown(HWND window, AppState& state,
                    bool allow_keep_open = true) noexcept;
void complete_deferred_close(HWND window, AppState& state) noexcept;
void finish_shutdown(HWND window, AppState& state) noexcept;
void run_shutdown_action(HWND window, AppState& state,
                         panedock::core::ShutdownAction action) noexcept;

void drag_state_changed(AppState& state, bool entering) noexcept {
    const auto action = state.shutdown_sequence.step(
        entering ? panedock::core::ShutdownEvent::drag_started
                 : panedock::core::ShutdownEvent::drag_finished);
    if (action == panedock::core::ShutdownAction::defer)
        run_shutdown_action(state.main_window, state, action);
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
    state.startup_notification.show(owner, state.chrome_font);
}

void finish_shutdown(HWND window, AppState& state) noexcept {
    if (state.shutdown_sequence.step(
            panedock::core::ShutdownEvent::teardown_started) !=
        panedock::core::ShutdownAction::destroy_views)
        return;
    // Keep the owner window visible while the synchronous Shell teardown runs.
    // The caption is the smallest truthful progress surface; do not destroy
    // the parent before every initialized ExplorerBrowser has been destroyed.
    set_main_window_title(window, state.diagnostic_mode, true);
    state.startup_realize_pending = false;
    ++state.startup_realize_generation;
    state.transfer_close_dialog.destroy();
    state.startup_notification.destroy();
    state.pinned_locations_dialog.destroy();
    revoke_drag_hover_targets(state);
    cancel_session_save_timer(state);
    destroy_panes(state);
    assert(panedock::explorer_host::live_view_count() == 0);
    if (state.shutdown_sequence.step(
            panedock::core::ShutdownEvent::views_destroyed) ==
        panedock::core::ShutdownAction::destroy_window) {
        DestroyWindow(window);
        PostQuitMessage(0);
    }
}

void run_shutdown_action(HWND window, AppState& state,
                         panedock::core::ShutdownAction action) noexcept {
    switch (action) {
        case panedock::core::ShutdownAction::defer:
            set_main_window_title(window, state.diagnostic_mode, true);
            if (state.shell_call_depth != 0 || state.shutdown_message_queued ||
                window == nullptr)
                return;
            state.shutdown_sequence.step(
                panedock::core::ShutdownEvent::deferred_shutdown_queued);
            if (PostMessageW(window, kDeferredShutdownMessage, 0, 0)) return;
            OutputDebugStringW(
                L"PaneDock: could not queue deferred shutdown\n");
            state.shutdown_sequence.step(
                panedock::core::ShutdownEvent::deferred_shutdown_queue_failed);
            if (IsWindow(window))
                begin_shutdown(window, state, !state.end_session_pending);
            return;

        case panedock::core::ShutdownAction::prompt_transfer:
            state.transfer_close_dialog.show(window);
            return;

        case panedock::core::ShutdownAction::save_session: {
            capture_window_placement(window, state);
            const bool save_succeeded = save_now(state, false, true);
            run_shutdown_action(
                window, state,
                state.shutdown_sequence.step(
                    save_succeeded
                        ? panedock::core::ShutdownEvent::save_succeeded
                        : panedock::core::ShutdownEvent::save_failed));
            return;
        }

        case panedock::core::ShutdownAction::prompt_save_failure: {
            state.shutdown_sequence.step(
                panedock::core::ShutdownEvent::save_prompt_started);
            const int answer = MessageBoxW(
                window,
                L"PaneDock could not save your session. Keep PaneDock open so "
                L"you can fix the storage problem and try again? Choose No to "
                L"close without saving recent changes.",
                L"PaneDock", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1);
            const auto prompt_result = state.shutdown_sequence.step(
                panedock::core::ShutdownEvent::save_prompt_finished);
            if (prompt_result ==
                panedock::core::ShutdownAction::destroy_views) {
                finish_shutdown(window, state);
                return;
            }
            if (answer != IDNO) {
                state.shutdown_sequence.step(
                    panedock::core::ShutdownEvent::save_keep_open);
                set_main_window_title(window, state.diagnostic_mode, false);
                return;
            }
            run_shutdown_action(
                window, state,
                state.shutdown_sequence.step(
                    panedock::core::ShutdownEvent::save_discarded));
            return;
        }

        case panedock::core::ShutdownAction::destroy_views:
            finish_shutdown(window, state);
            return;
        default:
            return;
    }
}

void begin_shutdown(HWND window, AppState& state,
                    bool allow_keep_open) noexcept {
    const auto action = state.shutdown_sequence.step(
        allow_keep_open ? panedock::core::ShutdownEvent::close_requested
                        : panedock::core::ShutdownEvent::end_session);
    run_shutdown_action(window, state, action);
}

void complete_deferred_close(HWND window, AppState& state) noexcept {
    if (!state.close_after_file_operation ||
        state.file_operation_call_active || state.file_operation_in_progress ||
        state.closing_ || window == nullptr)
        return;
    state.transfer_close_dialog.destroy();
    begin_shutdown(window, state, !state.end_session_pending);
}

void handle_transfer_close_dialog_result(HWND window, AppState& state) noexcept {
    const auto result = state.transfer_close_dialog.take_result();
    if (!result.has_value()) return;
    switch (*result) {
        case panedock::app_shell::TransferCloseDialog::Result::keep_open:
            state.shutdown_sequence.step(
                panedock::core::ShutdownEvent::transfer_keep_open);
            return;
        case panedock::app_shell::TransferCloseDialog::Result::
            close_after_transfer:
            state.shutdown_sequence.step(
                panedock::core::ShutdownEvent::transfer_close_after_transfer);
            complete_deferred_close(window, state);
            return;
        case panedock::app_shell::TransferCloseDialog::Result::
            cancel_and_close:
            state.shutdown_sequence.step(
                panedock::core::ShutdownEvent::transfer_cancel_and_close);
            return;
    }
}

void file_operation_started(void* context) noexcept {
    if (context != nullptr)
        static_cast<AppState*>(context)->shutdown_sequence.step(
            panedock::core::ShutdownEvent::file_operation_started);
}

void file_operation_finished(void* context) noexcept {
    if (context == nullptr) return;
    auto& state = *static_cast<AppState*>(context);
    state.shutdown_sequence.step(
        panedock::core::ShutdownEvent::file_operation_finished);
    if (state.main_window != nullptr)
        PostMessageW(state.main_window, kFileOperationFinishedMessage, 0, 0);
}

bool file_operation_cancel_requested(void* context) noexcept {
    if (context == nullptr) return false;
    const auto& state = *static_cast<AppState*>(context);
    return state.cancel_file_operation || state.shutdown_deferred ||
           state.closing_;
}

bool file_operation_setup_aborted(void* context) noexcept {
    if (context == nullptr) return false;
    const auto& state = *static_cast<AppState*>(context);
    return state.shutdown_deferred || state.closing_;
}

bool perform_clipboard_paste(HWND window, AppState& state,
                             std::size_t pane_index) noexcept {
    if (state.file_operation_call_active || state.closing_ ||
        state.shutdown_deferred)
        return true;
    if (pane_index >= kExplorerCount || !state.panes[pane_index].realized())
        return false;
    const std::wstring& parsing_name =
        state.panes[pane_index].host().location().parsing_name;
    if (parsing_name.empty()) return false;

    state.shutdown_sequence.step(
        panedock::core::ShutdownEvent::file_operation_call_started);
    panedock::file_operations::PasteResult result;
    {
        ShellCallScope shell_call(state);
        result = panedock::file_operations::paste_from_clipboard(
            window, parsing_name,
            {&state, file_operation_started, file_operation_finished,
             file_operation_cancel_requested,
             file_operation_setup_aborted});
    }
    state.shutdown_sequence.step(
        panedock::core::ShutdownEvent::file_operation_finished);
    state.shutdown_sequence.step(
        panedock::core::ShutdownEvent::file_operation_call_finished);
    complete_deferred_close(window, state);
    return state.shutdown_deferred || state.closing_ || result.handled;
}

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
        ShellCallScope shell_call(state);
        sidebar_created = state.sidebar.create(
            window, kGroupListId, state.sidebar_drag_target.Get());
    }
    if (state.shutdown_deferred || state.closing_) return 0;
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
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
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
        // PD-040: plain STATIC child used purely as a clipping container
        // (SetWindowRgn) and a parent HWND for ExplorerHost::initialize — it
        // never paints or handles messages of its own, so no custom window
        // class is needed.
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
            const int id = panedock::app_shell::encode_pane_control(
                pane_controls[button], index);
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
        if (!SetWindowSubclass(chrome.address_bar(), address_edit_proc, index,
                               reinterpret_cast<DWORD_PTR>(&state)))
            return -1;
        chrome.set_folder_context_button(CreateWindowExW(
            0, L"BUTTON", L"Folder context menu",
            WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | BS_PUSHBUTTON |
                BS_OWNERDRAW,
            0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(
                panedock::app_shell::encode_pane_control(
                    panedock::app_shell::PaneControl::folder_context, index)),
            GetModuleHandleW(nullptr), nullptr));
        if (chrome.folder_context_button() == nullptr) return -1;
        if (!SetWindowSubclass(
                chrome.folder_context_button(), hover_tracking_proc,
                static_cast<UINT_PTR>(
                    panedock::app_shell::encode_pane_control(
                        panedock::app_shell::PaneControl::folder_context,
                        index)),
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
    if (FAILED(apply_layout(window, state))) {
        // A single unreachable pane (offline drive, permission or AV block,
        // invalid stored location) must not prevent the app from opening. Keep
        // the window; the failing pane stays unrealized (blank) and the user is
        // told after create.
        append_startup_warning(
            state,
            L"PaneDock could not open the Shell view for one or more panes. "
            L"Some panes may be empty.");
    }
    if (state.shutdown_deferred || state.closing_) return 0;
    if (!register_tab_drag_hover_targets(window, state)) {
        OutputDebugStringW(
            L"PaneDock: RegisterDragDrop for tab strip failed\n");
        revoke_drag_hover_targets(state);
        append_startup_warning(
            state, L"PaneDock could not enable tab drag-and-drop.");
    }
    if (state.shutdown_deferred || state.closing_) return 0;
    return 0;
}

void handle_pane_command(
    AppState& state, panedock::app_shell::PaneControlId pane_control) {
    const std::size_t pane_index = pane_control.pane;
    switch (pane_control.control) {
        case panedock::app_shell::PaneControl::back:
            navigate_tab_history(state, pane_index, true);
            return;
        case panedock::app_shell::PaneControl::forward:
            navigate_tab_history(state, pane_index, false);
            return;
        case panedock::app_shell::PaneControl::up:
            navigate_up(state, pane_index);
            return;
        case panedock::app_shell::PaneControl::refresh:
            refresh_pane(state, pane_index);
            return;
        case panedock::app_shell::PaneControl::view_mode:
            show_view_mode_menu(state.main_window, state, pane_index);
            return;
        case panedock::app_shell::PaneControl::pinned:
            show_pinned_locations_menu(state.main_window, state, pane_index);
            return;
        case panedock::app_shell::PaneControl::folder_context: break;
        default: return;
    }
    const HWND folder_context_button =
        state.panes[pane_index].folder_context_button();
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size() ||
        !state.panes[pane_index].realized() ||
        folder_context_button == nullptr ||
        !IsWindowVisible(folder_context_button))
        return;
    set_active_pane(state.main_window, state, pane_index);
    if (state.closing_ || state.shutdown_deferred) return;
    if (active_pane_index(active_group(state)) != pane_index) return;
    RECT button_rect{};
    if (!GetWindowRect(folder_context_button, &button_rect))
        return;
    const POINT anchor{button_rect.left, button_rect.top};
    ShellCallScope shell_call(state);
    state.panes[pane_index].host().focus();
    (void)state.panes[pane_index].host().show_folder_context_menu(
        state.main_window, anchor);
}

bool handle_sidebar_command(HWND window, AppState& state, int id) {
    if (id == kGroupListId) {
        const auto selected = state.sidebar.selected_index();
        if (selected.has_value()) activate_group(window, state, *selected);
        return true;
    }
    switch (id) {
        case kNewGroupId: add_group(window, state); return true;
        case kDuplicateGroupId: duplicate_group(window, state); return true;
        case kRenameGroupId: state.sidebar.begin_rename(); return true;
        case kDeleteGroupId: delete_group(window, state); return true;
        case kMoveUpId: move_group(state, false); return true;
        case kMoveDownId: move_group(state, true); return true;
        default: return false;
    }
}

bool handle_global_command(HWND window, AppState& state, int id) {
    if (id == kCloseTabId || id == kCloseOtherTabsId ||
        id == kCloseAllTabsId || id == kCloseTabsToRightId) {
        const auto pane_index = state.tab_context_menu_pane;
        const std::string tab_id = state.tab_context_menu_tab_id;
        state.tab_context_menu_pane.reset();
        state.tab_context_menu_tab_id.clear();
        if (!pane_index.has_value() || !has_active_group(state) ||
            *pane_index >= active_group(state).panes.size())
            return true;
        if (id == kCloseTabId) {
            close_tab_in_pane(window, state, *pane_index, tab_id);
            return true;
        }

        const auto& tabs = active_group(state).panes[*pane_index].tabs;
        std::vector<std::string> tab_ids;
        if (id == kCloseOtherTabsId) {
            for (const auto& tab : tabs)
                if (tab.id != tab_id) tab_ids.push_back(tab.id);
        } else if (id == kCloseAllTabsId) {
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
            close_tab_in_pane(window, state, *pane_index, id_to_close);
        return true;
    }
    if (id >= kViewModeMenuIdBase &&
        id < kViewModeMenuIdBase + kViewModeMenuIdCount) {
        const int offset = id - kViewModeMenuIdBase;
        const std::size_t pane_index = static_cast<std::size_t>(
            offset / static_cast<int>(kViewModeOptions.size()));
        const std::size_t mode_index = static_cast<std::size_t>(
            offset % static_cast<int>(kViewModeOptions.size()));
        set_pane_view_mode(state, pane_index, kViewModeOptions[mode_index]);
        return true;
    }
    if (id >= kPinnedMenuIdBase &&
        id < kPinnedMenuIdBase + kPinnedMenuIdCount) {
        const int offset = id - kPinnedMenuIdBase;
        const std::size_t pane_index =
            static_cast<std::size_t>(offset / kPinnedMenuSlotsPerPane);
        const int item = offset % kPinnedMenuSlotsPerPane;
        if (!has_active_group(state) ||
            pane_index >= active_group(state).panes.size())
            return true;
        if (item == kPinnedMenuDesktopOffset ||
            item == kPinnedMenuThisPcOffset) {
            ShellCallScope shell_call(state);
            (void)navigate_pane(
                state, pane_index,
                location(std::wstring(kPinnedFixedParsingNames[
                    static_cast<std::size_t>(item)])));
            return true;
        }
        if (item >= kPinnedMenuLocationOffset && item < kPinnedMenuAddOffset) {
            const std::size_t location_index =
                static_cast<std::size_t>(item - kPinnedMenuLocationOffset);
            if (location_index < state.application.pinned_locations.size()) {
                ShellCallScope shell_call(state);
                (void)navigate_pane(
                    state, pane_index,
                    state.application.pinned_locations[location_index]);
            }
            return true;
        }
        if (item == kPinnedMenuAddOffset) {
            add_current_folder(state, pane_index);
            return true;
        }
        if (item == kPinnedMenuManageOffset) {
            show_pinned_locations_manager(window, state);
            return true;
        }
        return true;
    }
    if (id >= kLayoutButtonIdBase &&
        id < kLayoutButtonIdBase + static_cast<int>(kLayoutButtonIds.size())) {
        const std::size_t layout_index =
            static_cast<std::size_t>(id - kLayoutButtonIdBase);
        set_layout(window, state, kLayoutTemplates[layout_index]);
        return true;
    }
    return false;
}

bool draw_pane_control(
    const DRAWITEMSTRUCT& item, AppState& state,
    panedock::app_shell::PaneControlId pane_control) {
    struct PaneButtonDrawing final {
        panedock::app_shell::PaneControl control;
        std::size_t glyph;
        bool blend;
    };
    constexpr std::array pane_button_drawings{
        PaneButtonDrawing{panedock::app_shell::PaneControl::back, 0, false},
        PaneButtonDrawing{panedock::app_shell::PaneControl::forward, 1, false},
        PaneButtonDrawing{panedock::app_shell::PaneControl::up, 2, false},
        PaneButtonDrawing{panedock::app_shell::PaneControl::refresh, 3, false},
        PaneButtonDrawing{panedock::app_shell::PaneControl::view_mode, 4,
                          false},
        PaneButtonDrawing{panedock::app_shell::PaneControl::pinned, 5, false},
        PaneButtonDrawing{panedock::app_shell::PaneControl::folder_context, 6,
                          true}};
    const auto drawing = std::find_if(
        pane_button_drawings.begin(), pane_button_drawings.end(),
        [&](const auto& candidate) {
            return candidate.control == pane_control.control;
        });
    if (drawing == pane_button_drawings.end()) return false;
    draw_navigation_icon_button(
        item, drawing->glyph,
        state.owner_draw_hovered_button == item.hwndItem, drawing->blend);
    return true;
}

bool draw_global_control(const DRAWITEMSTRUCT& item, AppState& state) {
    for (const auto& chrome : state.panes) {
        if (chrome.status_bar() == item.hwndItem) {
            draw_status_bar(item, GetDpiForWindow(item.hwndItem));
            return true;
        }
    }
    if (item.CtlType == ODT_BUTTON && item.CtlID >= kLayoutButtonIdBase &&
        item.CtlID < kLayoutButtonIdBase +
                         static_cast<int>(kLayoutButtonIds.size())) {
        const std::size_t index =
            static_cast<std::size_t>(item.CtlID - kLayoutButtonIdBase);
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
    std::size_t group_index = item.itemID;
    bool placeholder = false;
    if (state.group_drag.has_value() && state.group_drag->dragging &&
        state.group_drag->target_index.has_value()) {
        const auto projected = panedock::core::reorder_source_index(
            state.application.groups.size(), state.group_drag->source_index,
            *state.group_drag->target_index, item.itemID);
        if (projected.has_value()) group_index = *projected;
        placeholder = *state.group_drag->target_index == item.itemID;
    }
    return state.sidebar.draw_item(
        const_cast<DRAWITEMSTRUCT*>(&item), group_index, placeholder);
}

bool handle_context_menu(HWND target, AppState& state, POINT screen) {
    const HWND window = state.main_window;
    const auto pane_index = tab_strip_index(state, target);
    if (pane_index.has_value()) {
        if (screen.x == -1 && screen.y == -1) return true;
        if (!has_active_group(state) ||
            *pane_index >= active_group(state).panes.size())
            return true;

        POINT client = screen;
        ScreenToClient(target, &client);
        const auto item = tab_item_at_point(state, target, client);
        const auto& tabs = active_group(state).panes[*pane_index].tabs;
        if (!item.has_value() || *item >= tabs.size()) return true;

        state.tab_context_menu_pane = *pane_index;
        state.tab_context_menu_tab_id = tabs[*item].id;
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) {
            state.tab_context_menu_pane.reset();
            state.tab_context_menu_tab_id.clear();
            return true;
        }
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCloseTabId),
                    L"Close Tab");
        AppendMenuW(menu, MF_STRING | (tabs.size() == 1 ? MF_GRAYED : 0),
                    static_cast<UINT_PTR>(kCloseOtherTabsId),
                    L"Close Other Tabs");
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kCloseAllTabsId),
                    L"Close All Tabs");
        AppendMenuW(menu,
                    MF_STRING |
                        (*item + 1 >= tabs.size() ? MF_GRAYED : 0),
                    static_cast<UINT_PTR>(kCloseTabsToRightId),
                    L"Close Tabs to the Right");
        SetForegroundWindow(window);
        const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                           screen.x, screen.y, 0, window,
                                           nullptr);
        DestroyMenu(menu);
        if (command != 0) {
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(command, 0), 0);
        } else {
            state.tab_context_menu_pane.reset();
            state.tab_context_menu_tab_id.clear();
        }
        return true;
    }
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

    state.sidebar.set_selected_index(index);
    refresh_sidebar(state);

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return true;
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kDuplicateGroupId),
                L"Duplicate Group");
    AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kRenameGroupId),
                L"Rename Group");
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
    SetForegroundWindow(window);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       point.x, point.y, 0, window, nullptr);
    DestroyMenu(menu);
    if (state.closing_ || state.shutdown_deferred) return true;
    if (command != 0)
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(command, 0), 0);
    return true;
}

std::optional<LRESULT> handle_global_mouse_message(
    HWND window, AppState& state, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_LBUTTONDOWN: {
            const POINT point = point_from_lparam(lparam);
            if (sidebar_boundary_at_point(window, state, point)) {
                state.sidebar_drag = AppState::SidebarDrag{
                    point.x,
                    std::clamp(state.application.sidebar_width,
                               kSidebarMinimumWidth, kSidebarMaximumWidth)};
                SetCapture(window);
                return 0;
            }
            if (has_active_group(state)) {
                state.splitter_drag =
                    splitter_at_point(window, state, active_group(state), point);
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
                    window, state, active_group(state),
                    point_from_lparam(lparam));
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
                    sidebar_boundary_at_point(window, state, point)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
                if (has_active_group(state)) {
                    const auto splitter = splitter_at_point(
                        window, state, active_group(state), point);
                    if (splitter.has_value()) {
                        SetCursor(LoadCursorW(
                            nullptr,
                            splitter->vertical ? IDC_SIZEWE : IDC_SIZENS));
                        return TRUE;
                    }
                }
            }
            break;
        case WM_PARENTNOTIFY:
            if (has_active_group(state) && LOWORD(wparam) == WM_MBUTTONDOWN) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                close_tab_at_point(window, state, point);
                return 0;
            }
            if (LOWORD(wparam) == WM_LBUTTONDOWN) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                const std::size_t pane = pane_at_point(window, state, point);
                if (pane < kExplorerCount) set_active_pane(window, state, pane);
            }
            return 0;
        default: break;
    }
    return std::nullopt;
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
    if (state != nullptr && (state->closing_ || state->shutdown_deferred) &&
        !close_request && message != WM_QUERYENDSESSION &&
        message != WM_ENDSESSION && message != WM_DESTROY &&
        message != WM_NCDESTROY && message != WM_PAINT &&
        message != WM_ERASEBKGND && message != WM_SETTEXT &&
        message != WM_NCPAINT && message != kDeferredShutdownMessage)
        return 0;

    if (state != nullptr && state->shell_call_depth != 0) {
        if (message == WM_COMMAND) {
            defer_shell_reentry_message(window, kDeferredCommandMessage,
                                        wparam, lparam);
            return 0;
        }
        if (message == kTabStripSelectionMessage) {
            defer_shell_reentry_message(window, kDeferredTabSelectionMessage,
                                        wparam, lparam);
            return 0;
        }
        if (message == kDragHoverMessage) {
            defer_shell_reentry_message(window, message, wparam, lparam);
            return 0;
        }
        if (message == WM_PARENTNOTIFY || message == WM_LBUTTONDOWN ||
            message == WM_LBUTTONDBLCLK || message == WM_LBUTTONUP) {
            defer_shell_reentry_message(window, message, wparam, lparam);
            return 0;
        }
    }

    switch (message) {
        case WM_CREATE: return create_main_window_children(window, *state);
        case kDeferredShutdownMessage:
            if (state != nullptr)
                run_shutdown_action(
                    window, *state,
                    state->shutdown_sequence.step(
                        panedock::core::ShutdownEvent::
                            deferred_shutdown_ready));
            return 0;
        case kDeferredCommandMessage:
            if (state != nullptr) {
                if (state->shell_call_depth != 0) {
                    defer_shell_reentry_message(
                        window, kDeferredCommandMessage, wparam, lparam);
                } else if (!state->closing_ && !state->shutdown_deferred) {
                    SendMessageW(window, WM_COMMAND, wparam, lparam);
                }
            }
            return 0;
        case kDeferredTabSelectionMessage:
            if (state != nullptr) {
                if (state->shell_call_depth != 0) {
                    defer_shell_reentry_message(
                        window, kDeferredTabSelectionMessage, wparam, lparam);
                } else if (!state->closing_ && !state->shutdown_deferred) {
                    SendMessageW(window, kTabStripSelectionMessage, wparam,
                                 lparam);
                }
            }
            return 0;
        case kDeferredLayoutMessage:
            if (state != nullptr && state->layout_message_queued) {
                state->layout_message_queued = false;
                state->layout_pending = false;
                if (!state->closing_ && !state->shutdown_deferred)
                    (void)apply_layout(window, *state, false, false);
            }
            return 0;
        case kDeferredRealizeMessage: {
            if (state == nullptr || !state->startup_realize_pending ||
                static_cast<UINT_PTR>(lparam) !=
                    state->startup_realize_generation)
                return 0;
            const HRESULT hr = realize_startup_panes(window, *state);
            if (state->shutdown_deferred || state->closing_) return 0;
            if (FAILED(hr)) {
                // A pane whose Shell view could not be realized stays blank;
                // update the non-blocking startup notification instead of
                // entering a modal loop after the app is already usable.
                append_startup_warning(
                    *state,
                    L"PaneDock could not open the Shell view for one or more "
                    L"panes. Some panes may be empty.");
                show_startup_notification(window, *state);
            }
            return 0;
        }
        case kTabStripSelectionMessage:
            if (state != nullptr && has_active_group(*state) &&
                wparam < state->panes.size() &&
                wparam < active_group(*state).panes.size()) {
                auto& pane = active_group(*state).panes[wparam];
                if (lparam == -1) {
                    add_tab_to_pane(window, *state, wparam);
                } else if (lparam >= 0 &&
                           static_cast<std::size_t>(lparam) < pane.tabs.size()) {
                    switch_active_tab(window, *state, wparam,
                                      pane.tabs[static_cast<std::size_t>(lparam)]
                                          .id);
                }
                return 0;
            }
            break;
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
                timer < kDragHoverTabTimerIdBase + kExplorerCount) {
                const std::size_t pane_index = static_cast<std::size_t>(
                    timer - kDragHoverTabTimerIdBase);
                if (auto* hover = dynamic_cast<panedock::app_shell::DragHoverTimer*>(
                        state->panes[pane_index].drag_hover_target()))
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
                    item->CtlID >= kLayoutButtonIdBase &&
                    item->CtlID < kLayoutButtonIdBase +
                                      static_cast<int>(kLayoutButtonIds.size())) {
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
            if (item->CtlType == ODT_BUTTON) {
                const auto pane_control =
                    panedock::app_shell::decode_pane_control(item->CtlID);
                if (pane_control.has_value() &&
                    draw_pane_control(*item, *state, *pane_control))
                    return TRUE;
            }
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
        case WM_CTLCOLOREDIT:
            if (state != nullptr) {
                const HWND control = reinterpret_cast<HWND>(lparam);
                bool is_pane_address_bar = false;
                for (const auto& chrome : state->panes) {
                    if (chrome.address_bar() == control) {
                        is_pane_address_bar = true;
                        break;
                    }
                }
                if (is_pane_address_bar) {
                    const HDC dc = reinterpret_cast<HDC>(wparam);
                    SetBkMode(dc, OPAQUE);
                    SetBkColor(dc, RGB(251, 252, 253));
                    SetTextColor(dc, RGB(76, 89, 107));
                    return reinterpret_cast<LRESULT>(
                        address_bar_background_brush());
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
                    paint_client_background(window, dc, *state);
                    RestoreDC(dc, saved);
                    EndPaint(window, &paint);
                    return 0;
                }
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
            if (const auto pane_control =
                    panedock::app_shell::decode_pane_control(id);
                pane_control.has_value()) {
                handle_pane_command(*state, *pane_control);
                return 0;
            }
            if (handle_sidebar_command(window, *state, id)) return 0;
            break;
        }
        case panedock::sidebar::kRenameCommitMessage:
            if (state != nullptr) {
                const auto selected = state->sidebar.selected_index();
                auto name = state->sidebar.take_rename_text();
                if (selected.has_value() && name.has_value() &&
                    *selected < state->application.groups.size()) {
                    const std::string id = state->application.groups[*selected].id;
                    if (panedock::core::rename_group(state->application, id,
                                                     std::move(*name))) {
                        refresh_sidebar(*state);
                        schedule_session_save(*state);
                    }
                }
            }
            return 0;
        case WM_SIZE:
            if (state != nullptr &&
                FAILED(apply_layout(window, *state, false, false)))
                OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            release_navigation_icon_font();
            release_brand_resources();
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (state != nullptr) refresh_ui_font(window, *state);
            if (state != nullptr && FAILED(apply_layout(window, *state)))
                OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
        case WM_LBUTTONDBLCLK:
        case WM_SETCURSOR:
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
                KillTimer(window, kSessionSaveTimerId);
                if (state->session_dirty) (void)save_now(*state);
                return 0;
            }
            if (timer == kDragHoverSidebarTimerId &&
                state->sidebar_drag_target != nullptr) {
                state->sidebar_drag_target->timer_expired();
                return 0;
            }
            if (timer >= kDragHoverTabTimerIdBase &&
                timer < kDragHoverTabTimerIdBase + kExplorerCount) {
                const std::size_t pane_index = static_cast<std::size_t>(
                    timer - kDragHoverTabTimerIdBase);
                if (auto* hover = dynamic_cast<panedock::app_shell::DragHoverTimer*>(
                        state->panes[pane_index].drag_hover_target()))
                    hover->timer_expired();
                return 0;
            }
            break;
        }
        case kFileOperationFinishedMessage:
            if (state != nullptr)
                complete_deferred_close(window, *state);
            return 0;
        case panedock::app_shell::kTransferCloseDialogResultMessage:
            if (state != nullptr)
                handle_transfer_close_dialog_result(window, *state);
            return 0;
        case WM_CLOSE:
            if (state != nullptr)
                run_shutdown_action(
                    window, *state,
                    state->shutdown_sequence.step(
                        state->end_session_pending
                            ? panedock::core::ShutdownEvent::end_session
                            : panedock::core::ShutdownEvent::close_requested));
            return 0;
        case WM_DESTROY:
            if (state != nullptr) {
                (void)state->shutdown_sequence.step(
                    panedock::core::ShutdownEvent::window_destroyed);
                state->startup_realize_pending = false;
                ++state->startup_realize_generation;
                state->pinned_locations_dialog.destroy();
                revoke_drag_hover_targets(*state);
                cancel_session_save_timer(*state);
                if (state->session_dirty && !state->shutdown_save_attempted) {
                    state->shutdown_sequence.step(
                        panedock::core::ShutdownEvent::save_started);
                    capture_window_placement(window, *state);
                    (void)save_now(*state, false, true);
                }
            }
            release_navigation_icon_font();
            release_brand_resources();
            release_address_bar_background_brush();
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
                    run_shutdown_action(
                        window, *state,
                        state->shutdown_sequence.step(
                            panedock::core::ShutdownEvent::end_session));
                } else {
                    state->shutdown_sequence.step(
                        panedock::core::ShutdownEvent::end_session_cancelled);
                }
            }
            return 0;
        default: break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool register_window_class(HINSTANCE instance) noexcept {
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
        ShellCallScope shell_call(state);
        directory = panedock::shell_core::session_directory();
    }
    if (state.shutdown_deferred || state.closing_) {
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
    state.session_directory = *directory;
    auto loaded = panedock::core::read_session(
        state.session_directory, default_application_state());
    const bool recovered_from_corruption = loaded.recovered_from_corruption;
    const auto session_source = loaded.source;
    const bool clean_shutdown = loaded.document.clean_shutdown;
    if (recovered_from_corruption) {
        OutputDebugStringW(L"PaneDock: session recovery source=");
        OutputDebugStringW(session_source_name(session_source));
        OutputDebugStringW(L"\n");
    }
    state.session_document = std::move(loaded.document);
    state.application = state.session_document.application;
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
    if (virtual_width > 0 && virtual_height > 0 && !placement.maximized &&
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
        assert(panedock::explorer_host::live_view_count() == 0);
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
    ShowWindow(window, placement.maximized ? SW_SHOWMAXIMIZED : show_command);
    UpdateWindow(window);
    state.startup_frame_only = false;
    // PD-135: a close can still arrive before the message loop consumes the
    // deferred-realize post. `proceed` prevents startup work from continuing
    // after the main HWND has been torn down; recoverable warnings below are
    // modeless and therefore do not create another nested modal loop.
    bool proceed = !state.closing_ && !state.shutdown_deferred &&
                   !state.quit_requested;
    // Queue realization before any startup warning enters a nested modal
    // loop. The warning remains acknowledgement-only, while its modal loop
    // can dispatch this message and show the pane content behind it.
    if (proceed && state.startup_realize_pending) {
        const UINT_PTR generation = ++state.startup_realize_generation;
        if (!PostMessageW(window, kDeferredRealizeMessage, 0,
                          static_cast<LPARAM>(generation))) {
            OutputDebugStringW(
                L"PaneDock: could not queue deferred Shell view realization\n");
            const HRESULT hr = realize_startup_panes(window, state);
            if (FAILED(hr) && !state.closing_ && !state.shutdown_deferred) {
                append_startup_warning(
                    state,
                    L"PaneDock could not open the Shell view for one or more "
                    L"panes. Some panes may be empty.");
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
        result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) break;
        if (!state.closing_ && !state.shutdown_deferred &&
            has_active_group(state)) {
            const std::size_t active = active_pane_index(active_group(state));
            const bool key_down = message.message == WM_KEYDOWN ||
                                  message.message == WM_SYSKEYDOWN;
            const bool control = GetKeyState(VK_CONTROL) < 0;
            const bool alt = GetKeyState(VK_MENU) < 0;
            const bool shift = GetKeyState(VK_SHIFT) < 0;
            if (message.message == WM_KEYDOWN && !control && !alt && !shift &&
                message.wParam == VK_F2 &&
                GetFocus() == state.sidebar.window()) {
                state.sidebar.begin_rename();
                continue;
            }
            if (key_down && control && !alt && !shift &&
                message.wParam == 'V' && !address_bar_has_focus(state) &&
                perform_clipboard_paste(window, state, active))
                continue;
            if (!address_bar_has_focus(state)) {
                HRESULT accelerator = S_FALSE;
                {
                    ShellCallScope shell_call(state);
                    accelerator =
                        state.panes[active].host().translate_accelerator(&message);
                }
                if (state.closing_ || state.shutdown_deferred) continue;
                if (accelerator == S_OK) continue;
            }
            if (key_down && control && !alt && message.wParam == 'T') {
                add_tab_to_pane(window, state, active);
                continue;
            }
            if (key_down && control && !alt && message.wParam == 'W') {
                close_tab_in_pane(window, state, active,
                                  active_tab(active_group(state).panes[active])
                                      .id);
                continue;
            }
            if (key_down && control && !alt && message.wParam == VK_TAB) {
                cycle_active_tab(window, state, active, shift);
                continue;
            }
            if (key_down && alt && !control && message.wParam == VK_LEFT) {
                navigate_tab_history(state, active, true);
                continue;
            }
            if (key_down && alt && !control && message.wParam == VK_RIGHT) {
                navigate_tab_history(state, active, false);
                continue;
            }
            if (key_down && !control && !alt &&
                message.wParam == VK_BACK && !address_bar_has_focus(state)) {
                navigate_up(state, active);
                continue;
            }
            if (key_down && !control && !alt && message.wParam == VK_TAB &&
                !address_bar_has_focus(state)) {
                const std::size_t count =
                    panedock::core::pane_count(active_group(state)
                                                   .layout_template);
                const std::size_t next = shift ? (active + count - 1) % count
                                               : (active + 1) % count;
                set_active_pane(window, state, next);
                continue;
            }
            if (message.message == WM_KEYDOWN && message.wParam == VK_F6) {
                const std::size_t count =
                    panedock::core::pane_count(active_group(state)
                                                   .layout_template);
                const std::size_t next = shift ? (active + count - 1) % count
                                               : (active + 1) % count;
                set_active_pane(window, state, next);
                continue;
            }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
        for (auto& chrome : state.panes)
            chrome.host().process_retry_request();
        apply_pinned_locations_dialog_result(state);
        handle_transfer_close_dialog_result(window, state);
        // win32: a nested modal loop can consume the WM_QUIT posted in
        // WM_CLOSE/WM_DESTROY. Exit on the flag so a close issued from inside a
        // shell popup / context menu still terminates the process.
        if (state.quit_requested) break;
    }
    exit_code = result < 0 ? 1 : static_cast<int>(message.wParam);
    if (state.quit_requested) exit_code = 0;

    state.pinned_locations_dialog.destroy();
    destroy_panes(state);
    assert(panedock::explorer_host::live_view_count() == 0);
    OleUninitialize();
    if (state.shutdown_clean_marker_armed && state.main_window_destroyed) {
        // Keep the durable marker false until Shell, the parent HWND and COM
        // have all gone away. If this write blocks or fails, the false marker
        // remains and the next startup can report the incomplete shutdown.
        state.session_document.application = state.application;
        state.session_document.clean_shutdown = true;
        if (!panedock::core::write_session(state.session_directory,
                                            state.session_document,
                                            flush_session_file)) {
            OutputDebugStringW(
                L"PaneDock: final clean-shutdown marker write failed\n");
        }
    }
    CloseHandle(single_instance_mutex);
    return exit_code;
}
