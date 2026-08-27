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
#include <vector>

#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include "app_shell/diagnostic_mode.h"
#include "app_shell/tab_overflow.h"
#include "core/layout.h"
#include "core/model.h"
#include "core/session.h"
#include "explorer_host/explorer_host.h"
#include "resource.h"
#include "sidebar/sidebar.h"

namespace {

constexpr wchar_t kWindowClassName[] = L"PaneDockMainWindow";
constexpr std::size_t kExplorerCount = 4;
constexpr int kLayoutBarHeight = 44;
constexpr int kLayoutButtonHeight = 30;
constexpr int kLayoutButtonWidth = 30;
constexpr int kPaneCanvasPadding = 15;
constexpr int kPaneDividerThickness = 8;
constexpr int kActivePaneIndicatorHeight = 3;
constexpr int kSidebarHeadingHeight = 20;
constexpr int kTabStripHeight = 31;
constexpr int kTabStripIdBase = 200;
constexpr UINT kTabStripSelectionMessage = WM_APP + 49;
// PD-049: content-sized tabs with a fixed add button at the right edge.
constexpr int kTabMinWidth = 72;
constexpr int kTabMaxWidth = 200;
constexpr int kTabAddButtonWidth = 36;
// PD-073: reserved only while the tab content overflows its viewport.
constexpr int kTabScrollButtonWidth = 28;
// PD-080: keep the larger PD-073 rect as the hit-test target, but paint a
// compact button inside it so the visual control is smaller than the tab row.
constexpr int kTabScrollButtonVisualWidth = 18;
constexpr int kTabScrollButtonVisualHeight = 16;
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
constexpr int kNavigationBarHeight = 28;
constexpr int kStatusBarHeight = 24;
constexpr int kNavigationButtonWidth = 32;
constexpr int kNavigationGlyphSize = 16;
constexpr std::array<wchar_t, 5> kNavigationGlyphs{
    L'\uE72B', L'\uE72A', L'\uE74A', L'\uE72C', L'\uE80A'};
// PD-031: rounded light-gray pill drawn behind the address bar EDIT to fake
// a rounded input box (see docs/tickets/PD-031-*.md decision 2). Radius is
// smaller than the design mock's 6px .location radius because the fixed
// kNavigationBarHeight budget (28px@96dpi) does not leave much room for an
// inset that must exceed the radius on every side while still leaving the
// EDIT control tall enough to show text.
constexpr int kAddressBarBackgroundRadius = 4;
constexpr int kAddressBarInset = 6;
constexpr int kBackButtonIdBase = 300;
constexpr int kForwardButtonIdBase = 310;
constexpr int kUpButtonIdBase = 320;
constexpr int kAddressBarIdBase = 330;
constexpr int kRefreshButtonIdBase = 340;
constexpr int kViewModeButtonIdBase = 350;
// View-mode popup commands: eight IDs per pane, 360-391, kept separate from
// the navigation buttons and layout commands above.
constexpr int kViewModeMenuIdBase = 360;
constexpr std::size_t kViewModeOptionCount = 8;
constexpr int kViewModeMenuIdCount =
    static_cast<int>(kExplorerCount * kViewModeOptionCount);
constexpr int kLayoutButtonIdBase = 400;
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
constexpr std::array<int, 5> kLayoutButtonIds{
    kLayoutButtonIdBase, kLayoutButtonIdBase + 1, kLayoutButtonIdBase + 2,
    kLayoutButtonIdBase + 3, kLayoutButtonIdBase + 4};
constexpr std::array<const wchar_t*, 5> kLayoutButtonLabels{
    L"Single", L"Left / Right", L"Top / Bottom", L"Three", L"Four"};
constexpr std::array<panedock::core::LayoutTemplate, 5> kLayoutTemplates{
    panedock::core::LayoutTemplate::single,
    panedock::core::LayoutTemplate::left_right,
    panedock::core::LayoutTemplate::top_bottom,
    panedock::core::LayoutTemplate::three_pane,
    panedock::core::LayoutTemplate::four_pane_grid};
struct ViewModeSelection final {
    FOLDERVIEWMODE mode;
    int image_size;
};
struct ViewModeOption final {
    FOLDERVIEWMODE mode;
    int image_size;
    const wchar_t* label;
};
// These are the four 96-DPI image sizes used by Windows File Explorer's
// Extra large/Large/Medium/Small icon commands. They are image pixels passed
// to IFolderView2, not FOLDERVIEWMODE values; see the PD-079 handoff for the
// Windows Explorer/API verification.
constexpr int kExtraLargeIconSize = 256;
constexpr int kLargeIconSize = 96;
constexpr int kMediumIconSize = 48;
constexpr int kSmallIconSize = 16;
constexpr std::array<ViewModeOption, kViewModeOptionCount> kViewModeOptions{{
    {FVM_ICON, kExtraLargeIconSize, L"Extra large icons"},
    {FVM_ICON, kLargeIconSize, L"Large icons"},
    {FVM_ICON, kMediumIconSize, L"Medium icons"},
    {FVM_ICON, kSmallIconSize, L"Small icons"},
    {FVM_LIST, -1, L"List"},
    {FVM_DETAILS, -1, L"Details"},
    {FVM_TILE, -1, L"Tiles"},
    {FVM_CONTENT, -1, L"Content"},
}};
const std::array<std::wstring, kExplorerCount> kDefaultLocations{
    L"C:\\", L"C:\\Windows", L"C:\\Users", L"C:\\Program Files"};
constexpr UINT kDragHoverDelayMilliseconds = 800;
constexpr UINT_PTR kDragHoverSidebarTimerId = 0xD034;
constexpr UINT_PTR kDragHoverTabTimerIdBase = 0xD040;

class DragHoverTarget final : public IDropTarget {
public:
    using HitTest = std::function<std::optional<std::size_t>(POINT)>;
    using HoverCallback = std::function<void(std::size_t)>;

    DragHoverTarget(HWND timer_window, UINT_PTR timer_id, HitTest hit_test,
                    HoverCallback hover_callback)
        : timer_window_(timer_window),
          timer_id_(timer_id),
          hit_test_(std::move(hit_test)),
          hover_callback_(std::move(hover_callback)) {}

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

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining =
            references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*, DWORD, POINTL point,
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
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject*, DWORD, POINTL,
                                    DWORD* effect) override {
        cancel_hover();
        if (effect == nullptr) return E_INVALIDARG;
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }

    void timer_expired() noexcept {
        if (!hover_index_.has_value() || hover_triggered_ || invoking_)
            return;
        const std::size_t index = *hover_index_;
        hover_triggered_ = true;
        stop_timer();
        invoking_ = true;
        try {
            hover_callback_(index);
        } catch (...) {
            OutputDebugStringW(L"PaneDock: drag hover callback failed\n");
        }
        invoking_ = false;
    }

private:
    ~DragHoverTarget() { cancel_hover(); }

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
    std::atomic<ULONG> references_{1};
    std::optional<std::size_t> hover_index_;
    bool timer_running_{false};
    bool hover_triggered_{false};
    bool invoking_{false};
};

Microsoft::WRL::ComPtr<DragHoverTarget> make_drag_hover_target(
    HWND timer_window, UINT_PTR timer_id, DragHoverTarget::HitTest hit_test,
    DragHoverTarget::HoverCallback hover_callback) {
    Microsoft::WRL::ComPtr<DragHoverTarget> target;
    auto* raw = new (std::nothrow)
        DragHoverTarget(timer_window, timer_id, std::move(hit_test),
                        std::move(hover_callback));
    if (raw != nullptr) target.Attach(raw);
    return target;
}

void write_live_view_count() noexcept {
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
    std::array<panedock::explorer_host::ExplorerHost, kExplorerCount> explorers;
    std::array<bool, kExplorerCount> realized{};
    panedock::core::ApplicationState application;
    panedock::core::SessionDocument session_document;
    std::filesystem::path session_directory;
    std::optional<Splitter> splitter_drag;
    struct TabDrag final {
        HWND strip{nullptr};
        std::size_t pane_index{};
        std::size_t source_index{};
        std::string tab_id;
        POINT start{};
        bool dragging{};
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
    std::optional<std::size_t> layout_hover_index;
    HWND layout_tooltip{nullptr};
    HWND empty_message{nullptr};
    struct TabVisual final {
        std::wstring text;
        RECT rect{};
    };
    std::array<HWND, kExplorerCount> tab_strips{};
    std::array<std::vector<TabVisual>, kExplorerCount> tab_visuals{};
    std::array<std::optional<RECT>, kExplorerCount> tab_placeholder_rects{};
    std::array<RECT, kExplorerCount> tab_add_rects{};
    // PD-073: UI-only scroll state; never persisted with the session.
    std::array<std::array<RECT, 2>, kExplorerCount>
        tab_scroll_button_rects{};
    std::array<int, kExplorerCount> tab_scroll_offsets{};
    std::array<int, kExplorerCount> tab_scroll_max_offsets{};
    // A tab index is stored here; pane.tabs.size() represents the add button.
    std::array<std::optional<std::size_t>, kExplorerCount> tab_hover_indices{};
    // PD-040: one clipping container child window per pane, sitting between
    // the main window and each ExplorerHost's IExplorerBrowser view. Only
    // this container's HWND gets SetWindowRgn'd for full-corner rounding —
    // IExplorerBrowser's own HWND, Advise/Unadvise and site contract are
    // untouched (see PD-040 override of PD-030).
    std::array<HWND, kExplorerCount> explorer_containers{};
    std::array<std::optional<RECT>, kExplorerCount> laid_out_pane_rects{};
    std::array<HWND, kExplorerCount> address_bars{};
    std::array<HWND, kExplorerCount> status_bars{};
    std::array<HWND, kExplorerCount> back_buttons{};
    std::array<HWND, kExplorerCount> forward_buttons{};
    std::array<HWND, kExplorerCount> up_buttons{};
    std::array<HWND, kExplorerCount> refresh_buttons{};
    std::array<HWND, kExplorerCount> view_mode_buttons{};
    std::array<bool, kExplorerCount> suppress_history_record{};
    Microsoft::WRL::ComPtr<DragHoverTarget> sidebar_drag_target;
    std::array<Microsoft::WRL::ComPtr<DragHoverTarget>, kExplorerCount>
        tab_drag_targets{};
};

void revoke_drag_hover_targets(AppState& state) noexcept {
    state.sidebar.revoke_drag_drop();
    state.sidebar_drag_target.Reset();
    for (std::size_t index = 0; index < state.tab_drag_targets.size();
         ++index) {
        if (state.tab_drag_targets[index] != nullptr)
            RevokeDragDrop(state.tab_strips[index]);
        state.tab_drag_targets[index].Reset();
    }
}

std::optional<std::filesystem::path> session_directory() noexcept {
    PWSTR local_app_data = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                    &local_app_data))) {
        return std::nullopt;
    }
    const std::filesystem::path directory =
        std::filesystem::path(local_app_data) / L"PaneDock";
    CoTaskMemFree(local_app_data);
    return directory;
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

panedock::core::ApplicationState default_application_state() {
    panedock::core::GroupState group;
    group.id = "default";
    group.name = L"Group 1";
    group.layout_template = panedock::core::LayoutTemplate::four_pane_grid;
    group.divider_ratios = panedock::core::default_divider_ratios(
        group.layout_template);
    for (std::size_t index = 0; index < kExplorerCount; ++index) {
        const std::string suffix = std::to_string(index);
        group.panes.push_back({"pane-" + suffix,
                               {{"tab-" + suffix,
                                 location(kDefaultLocations[index]), {}, {},
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

RECT to_win32_rect(const panedock::core::PaneRect& rect) noexcept {
    return {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
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
        scaled_value(window, kNavigationButtonWidth), pane_width / 6);
    const int address_left = pane_rect.left + button_width * 5;
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
                                    : hovered   ? RGB(242, 245, 248)
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
    if ((item.itemState & ODS_FOCUS) != 0) DrawFocusRect(item.hDC, &item.rcItem);
}

void draw_layout_segment_background(HDC dc, RECT rect, UINT dpi) noexcept {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    const int radius = std::max(
        1, MulDiv(kAddressBarBackgroundRadius, static_cast<int>(dpi), 96));
    HBRUSH fill = CreateSolidBrush(RGB(251, 252, 253));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(217, 225, 234));
    if (fill != nullptr && border != nullptr) {
        const HGDIOBJ old_brush = SelectObject(dc, fill);
        const HGDIOBJ old_pen = SelectObject(dc, border);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius,
                  radius);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
    }
    if (border != nullptr) DeleteObject(border);
    if (fill != nullptr) DeleteObject(fill);
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
    RECT glyph_rect = item.rcItem;
    const int drawn = DrawTextW(item.hDC, &glyph, 1, &glyph_rect,
                                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
            case 4:  // fallback view: four small squares
                for (int row = -1; row <= 1; row += 2)
                    for (int column = -1; column <= 1; column += 2)
                        Rectangle(item.hDC, cx + column * half / 2 - 1,
                                  cy + row * half / 2 - 1,
                                  cx + column * half / 2 + 2,
                                  cy + row * half / 2 + 2);
                break;
            default:
                break;
        }
        SelectObject(item.hDC, previous);
        DeleteObject(pen);
    }
}

void draw_navigation_icon_button(const DRAWITEMSTRUCT& item,
                                 std::size_t glyph_kind) noexcept {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
    if (background != nullptr) {
        FillRect(item.hDC, &item.rcItem, background);
        DeleteObject(background);
    }

    const COLORREF color = disabled ? RGB(190, 197, 209) : RGB(90, 102, 122);
    const int size =
        std::max(4, scaled_value(item.hwndItem, kNavigationGlyphSize));
    if (glyph_kind < kNavigationGlyphs.size() &&
        draw_navigation_font_glyph(item, kNavigationGlyphs[glyph_kind],
                                   color)) {
        if ((item.itemState & ODS_FOCUS) != 0)
            DrawFocusRect(item.hDC, &item.rcItem);
        return;
    }

    // The font-failure path keeps all five controls visible without the
    // platform icon font.
    draw_navigation_fallback_glyph(item, glyph_kind, color, size);
    if ((item.itemState & ODS_FOCUS) != 0) DrawFocusRect(item.hDC, &item.rcItem);
}

void draw_sidebar_action_button(const DRAWITEMSTRUCT& item,
                                const wchar_t* label) noexcept {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const COLORREF border = disabled ? RGB(232, 235, 239) : RGB(223, 229, 236);
    const COLORREF text_color = disabled ? RGB(180, 188, 199) : RGB(82, 96, 117);
    HBRUSH fill = CreateSolidBrush(RGB(255, 255, 255));
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    if (fill != nullptr && pen != nullptr) {
        const HGDIOBJ old_brush = SelectObject(item.hDC, fill);
        const HGDIOBJ old_pen = SelectObject(item.hDC, pen);
        const int radius =
            std::max(4, static_cast<int>(item.rcItem.bottom - item.rcItem.top) /
                            4);
        RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right,
                  item.rcItem.bottom, radius, radius);
        SelectObject(item.hDC, old_brush);
        SelectObject(item.hDC, old_pen);
    }
    if (fill != nullptr) DeleteObject(fill);
    if (pen != nullptr) DeleteObject(pen);

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
    HBRUSH background = CreateSolidBrush(RGB(249, 250, 251));
    if (background != nullptr) {
        FillRect(item.hDC, &rect, background);
        DeleteObject(background);
    }

    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    const int separator_height = std::min(
        std::max(1, MulDiv(1, static_cast<int>(dpi), 96)), height);
    if (separator_height > 0) {
        RECT separator = rect;
        separator.bottom = separator.top + separator_height;
        HBRUSH line = CreateSolidBrush(RGB(232, 237, 242));
        if (line != nullptr) {
            FillRect(item.hDC, &separator, line);
            DeleteObject(line);
        }
    }

    std::array<wchar_t, 256> text{};
    GetWindowTextW(item.hwndItem, text.data(),
                   static_cast<int>(text.size()));
    RECT text_rect = rect;
    text_rect.top += separator_height;
    const HFONT font = reinterpret_cast<HFONT>(
        SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font =
        font != nullptr ? SelectObject(item.hDC, font) : nullptr;
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, RGB(100, 116, 139));
    DrawTextW(item.hDC, text.data(), -1, &text_rect,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if (old_font != nullptr) SelectObject(item.hDC, old_font);
}

RECT pane_area(HWND window) noexcept {
    RECT area = client_rect(window);
    area.left = std::min(area.right,
                         area.left + scaled_value(
                                         window,
                                         panedock::sidebar::kSidebarWidth));
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

RECT pane_content_area(HWND window) noexcept {
    RECT area = pane_area(window);
    const LayoutMetrics metrics = layout_metrics(window);
    const int padding = scaled_value(window, kPaneCanvasPadding);
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
    HWND window, const panedock::core::GroupState& group) {
    const RECT client = pane_content_area(window);
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
                                const panedock::core::GroupState& group) {
    const auto rects = layout_rects(window, group);
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
    }
    return {};
}

std::optional<Splitter> splitter_at_point(
    HWND window, const panedock::core::GroupState& group, POINT point) {
    for (const Splitter& splitter : splitters(window, group)) {
        if (PtInRect(&splitter.rect, point)) return splitter;
    }
    return std::nullopt;
}

void refresh_navigation_buttons(AppState& state, std::size_t pane_index) {
    if (pane_index >= kExplorerCount) return;
    const bool visible = has_active_group(state) &&
                         pane_index < active_group(state).panes.size();
    if (!visible) {
        EnableWindow(state.back_buttons[pane_index], FALSE);
        EnableWindow(state.forward_buttons[pane_index], FALSE);
        EnableWindow(state.up_buttons[pane_index], FALSE);
        return;
    }
    const auto& tab = active_tab(active_group(state).panes[pane_index]);
    EnableWindow(state.back_buttons[pane_index],
                 !state.suppress_history_record[pane_index] &&
                     panedock::core::can_navigate_tab_back(tab));
    EnableWindow(state.forward_buttons[pane_index],
                 !state.suppress_history_record[pane_index] &&
                     panedock::core::can_navigate_tab_forward(tab));
    EnableWindow(state.up_buttons[pane_index], TRUE);
}

void refresh_navigation_chrome(AppState& state, std::size_t pane_index) {
    if (pane_index >= kExplorerCount) return;
    refresh_navigation_buttons(state, pane_index);
    const wchar_t* text = L"";
    if (has_active_group(state) &&
        pane_index < active_group(state).panes.size()) {
        text = active_tab(active_group(state).panes[pane_index])
                   .location.parsing_name.c_str();
    }
    SetWindowTextW(state.address_bars[pane_index], text);
}

std::string view_mode_name(FOLDERVIEWMODE mode, int image_size = -1) {
    switch (mode) {
        case FVM_ICON:
            return image_size > 0 ? "FVM_ICON:" + std::to_string(image_size)
                                  : "FVM_ICON";
        case FVM_SMALLICON:
            return image_size > 0 ? "FVM_ICON:" + std::to_string(image_size)
                                  : "FVM_ICON:16";
        case FVM_LIST: return "FVM_LIST";
        case FVM_DETAILS: return "FVM_DETAILS";
        case FVM_TILE: return "FVM_TILE";
        case FVM_CONTENT: return "FVM_CONTENT";
        default: return {};
    }
}

std::optional<ViewModeSelection> parse_view_mode(std::string_view name) {
    if (name == "FVM_ICON") return ViewModeSelection{FVM_ICON, kLargeIconSize};
    if (name == "FVM_SMALLICON") return ViewModeSelection{FVM_ICON, kSmallIconSize};
    if (name == "FVM_LIST") return ViewModeSelection{FVM_LIST, -1};
    if (name == "FVM_DETAILS") return ViewModeSelection{FVM_DETAILS, -1};
    if (name == "FVM_TILE") return ViewModeSelection{FVM_TILE, -1};
    if (name == "FVM_CONTENT") return ViewModeSelection{FVM_CONTENT, -1};

    constexpr std::string_view prefix = "FVM_ICON:";
    if (name.starts_with(prefix)) {
        int image_size{};
        const auto first = name.data() + prefix.size();
        const auto last = name.data() + name.size();
        const auto parsed = std::from_chars(first, last, image_size);
        if (parsed.ec == std::errc{} && parsed.ptr == last && image_size > 0)
            return ViewModeSelection{FVM_ICON, image_size};
    }
    return std::nullopt;
}

void capture_pane_view_mode(AppState& state, std::size_t pane_index) {
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size() ||
        !state.realized[pane_index]) return;
    FOLDERVIEWMODE mode{};
    int image_size = -1;
    if (SUCCEEDED(state.explorers[pane_index].get_view_mode(mode, &image_size))) {
        const std::string name = view_mode_name(mode, image_size);
        if (!name.empty())
            active_tab(active_group(state).panes[pane_index]).view_mode = name;
    }
}

void apply_pane_view_mode(AppState& state, std::size_t pane_index) {
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size() ||
        !state.realized[pane_index]) return;
    auto& tab = active_tab(active_group(state).panes[pane_index]);
    if (const auto selection = parse_view_mode(tab.view_mode);
        selection.has_value()) {
        (void)state.explorers[pane_index].set_view_mode(
            selection->mode, selection->image_size);
    }
    capture_pane_view_mode(state, pane_index);
}

void refresh_status_bar(AppState& state, std::size_t pane_index) noexcept {
    if (pane_index >= kExplorerCount || state.status_bars[pane_index] == nullptr)
        return;
    panedock::explorer_host::ExplorerHost::ItemCounts counts;
    if (FAILED(state.explorers[pane_index].item_counts(counts))) {
        SetWindowTextW(state.status_bars[pane_index], L"");
        return;
    }
    std::wstring text = std::to_wstring(counts.total) + L" items";
    if (counts.selected != 0) {
        text += L"   ";
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
                text += L"   ";
                text += size_text.data();
            }
        }
    }
    SetWindowTextW(state.status_bars[pane_index], text.c_str());
}

std::wstring tab_display_text(const panedock::core::TabState& tab) {
    const auto& parsing_name = tab.location.parsing_name;
    const std::size_t separator = parsing_name.find_last_of(L"\\/");
    if (separator == std::wstring::npos || separator + 1 == parsing_name.size())
        return parsing_name;
    return parsing_name.substr(separator + 1);
}

int clamp_tab_scroll_offset(AppState& state, std::size_t pane_index,
                            int requested, int content_width,
                            int viewport_width) noexcept {
    const int maximum = std::max(
        0, content_width - viewport_width);
    state.tab_scroll_max_offsets[pane_index] = maximum;
    state.tab_scroll_offsets[pane_index] =
        panedock::app_shell::clamp_tab_scroll_offset(
            requested, content_width, viewport_width);
    return state.tab_scroll_offsets[pane_index];
}

void apply_tab_item_size(AppState& state, std::size_t pane_index,
                         bool reveal_active = false) {
    if (pane_index >= state.tab_strips.size()) return;
    const HWND strip = state.tab_strips[pane_index];
    RECT client{};
    GetClientRect(strip, &client);
    const int min_width = scaled_value(strip, kTabMinWidth);
    const int max_width = scaled_value(strip, kTabMaxWidth);
    const int add_width = scaled_value(strip, kTabAddButtonWidth);
    const int available = std::max(0, static_cast<int>(client.right) - add_width);
    auto& visuals = state.tab_visuals[pane_index];
    auto& placeholder = state.tab_placeholder_rects[pane_index];
    placeholder.reset();
    std::vector<int> widths;
    widths.reserve(visuals.size());
    const int text_reserve = scaled_value(
        strip, 2 * kTabTextHorizontalPadding + kTabCloseButtonSpace);
    HDC dc = GetDC(strip);
    const HFONT font = state.chrome_font;
    HGDIOBJ previous = dc == nullptr ? nullptr : SelectObject(dc, font);
    for (const auto& visual : visuals) {
        SIZE size{};
        if (dc != nullptr)
            GetTextExtentPoint32W(dc, visual.text.c_str(),
                                  static_cast<int>(visual.text.size()), &size);
        widths.push_back(std::clamp(static_cast<int>(size.cx) + text_reserve,
                                    min_width, max_width));
    }
    if (dc != nullptr) {
        SelectObject(dc, previous);
        ReleaseDC(strip, dc);
    }
    const int total = std::accumulate(widths.begin(), widths.end(), 0);
    if (total > available && total > 0) {
        for (int& width : widths)
            width = std::max(min_width, MulDiv(width, available, total));
    }
    const int content_width = std::accumulate(widths.begin(), widths.end(), 0);
    const auto viewport = panedock::app_shell::tab_strip_viewport(
        content_width, available, scaled_value(strip, kTabScrollButtonWidth));
    state.tab_add_rects[pane_index] = {
        available, 0, client.right, client.bottom};
    state.tab_scroll_button_rects[pane_index] = {};
    if (viewport.overflow && viewport.scroll_button_width > 0) {
        const int button_left = viewport.width;
        state.tab_scroll_button_rects[pane_index][0] = {
            button_left, 0, button_left + viewport.scroll_button_width,
            client.bottom};
        state.tab_scroll_button_rects[pane_index][1] = {
            button_left + viewport.scroll_button_width, 0, available,
            client.bottom};
    }
    std::vector<std::size_t> order(visuals.size());
    std::iota(order.begin(), order.end(), 0);
    if (state.tab_drag.has_value() && state.tab_drag->dragging &&
        state.tab_drag->pane_index == pane_index &&
        state.tab_drag->target_index.has_value() &&
        state.tab_drag->source_index < order.size() &&
        *state.tab_drag->target_index < order.size()) {
        const std::size_t source = state.tab_drag->source_index;
        order.erase(order.begin() + source);
        order.insert(order.begin() + *state.tab_drag->target_index, source);
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
    int active_left = 0;
    int active_right = 0;
    int content_x = 0;
    if (active_index.has_value()) {
        for (const std::size_t index : order) {
            if (index == *active_index) {
                active_left = content_x;
                active_right = content_x + widths[index];
                break;
            }
            content_x += widths[index];
        }
    }
    int requested_offset = state.tab_scroll_offsets[pane_index];
    if (active_index.has_value()) {
        if (active_left < requested_offset)
            requested_offset = active_left;
        else if (active_right > requested_offset + viewport.width)
            requested_offset = active_right - viewport.width;
    }
    const int scroll_offset = clamp_tab_scroll_offset(
        state, pane_index, requested_offset, content_width, viewport.width);
    int x = 0;
    for (const std::size_t index : order) {
        const int width = widths[index];
        const RECT rect{x - scroll_offset, 0, x + width - scroll_offset,
                        client.bottom};
        if (state.tab_drag.has_value() && state.tab_drag->dragging &&
            state.tab_drag->pane_index == pane_index &&
            state.tab_drag->target_index.has_value() &&
            index == state.tab_drag->source_index) {
            visuals[index].rect = {};
            placeholder = rect;
        } else {
            visuals[index].rect = rect;
        }
        x += width;
    }
#ifndef NDEBUG
    for (std::size_t index = 0; index < visuals.size(); ++index) {
        const RECT& rect = visuals[index].rect;
        const bool empty = rect.left == 0 && rect.top == 0 &&
                           rect.right == 0 && rect.bottom == 0;
        assert(empty || rect.right - rect.left == widths[index]);
    }
#endif
    InvalidateRect(strip, nullptr, FALSE);
}

void refresh_tab_strip(AppState& state, std::size_t pane_index) {
    if (pane_index >= state.tab_strips.size()) return;
    state.tab_hover_indices[pane_index].reset();
    state.tab_visuals[pane_index].clear();
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size()) {
        apply_tab_item_size(state, pane_index);
        refresh_navigation_chrome(state, pane_index);
        return;
    }

    const auto& pane = active_group(state).panes[pane_index];
    for (const auto& tab : pane.tabs)
        state.tab_visuals[pane_index].push_back({tab_display_text(tab), {}});
    apply_tab_item_size(state, pane_index, true);
    refresh_navigation_chrome(state, pane_index);
}

void refresh_tab_strips(AppState& state) {
    for (std::size_t index = 0; index < state.tab_strips.size(); ++index)
        refresh_tab_strip(state, index);
}

void capture_pane_location(AppState& state, std::size_t pane_index) {
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    if (pane_index >= group.panes.size() || !state.realized[pane_index] ||
        state.explorers[pane_index].location().empty()) return;
    auto& tab = active_tab(group.panes[pane_index]);
    tab.location.parsing_name = state.explorers[pane_index].location();
    capture_pane_view_mode(state, pane_index);
    if (!tab.history.empty() && tab.history_index < tab.history.size()) {
        tab.history[tab.history_index] = tab.location;
    }
}

void capture_locations(AppState& state) {
    if (!has_active_group(state)) return;
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

void layout_sidebar(HWND window, AppState& state) noexcept {
    const RECT client = client_rect(window);
    const int width = std::min(static_cast<int>(client.right - client.left),
                               scaled_value(window,
                                            panedock::sidebar::kSidebarWidth));
    const int margin = scaled_value(window, 8);
    const int gap = scaled_value(window, 4);
    const int brand_height = scaled_value(window, kBrandBarHeight);
    const int heading_height = scaled_value(window, kSidebarHeadingHeight);
    const int button_height = scaled_value(window, 28);
    const int controls_height = static_cast<int>(state.sidebar_buttons.size()) *
                                    button_height +
                                static_cast<int>(state.sidebar_buttons.size() - 1) * gap;
    SetWindowPos(state.group_label, nullptr, margin, brand_height + margin,
                 std::max(0, width - 2 * margin), heading_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(state.group_label, SW_SHOW);
    const int list_top = brand_height + margin + heading_height + gap;
    RECT list_rect{margin, list_top, std::max(margin, width - margin),
                   std::max(list_top, static_cast<int>(client.bottom) - margin -
                                             controls_height - gap)};
    state.sidebar.set_rect(list_rect, GetDpiForWindow(window));
    int y = list_rect.bottom + gap;
    for (HWND button : state.sidebar_buttons) {
        SetWindowPos(button, nullptr, margin, y,
                     std::max(0, width - 2 * margin), button_height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        y += button_height + gap;
    }
    const RECT panes = pane_area(window);
    SetWindowPos(state.empty_message, nullptr, panes.left, panes.top,
                 panes.right - panes.left, panes.bottom - panes.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void layout_header(HWND window, AppState& state) noexcept {
    const RECT client = client_rect(window);
    const int sidebar_width = std::min(
        static_cast<int>(client.right - client.left),
        scaled_value(window, panedock::sidebar::kSidebarWidth));
    const int margin = scaled_value(window, 12);
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
        SetWindowPos(button, nullptr, x,
                     std::max(0, (header_height - button_height) / 2),
                     button_width, button_height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(button, SW_SHOW);
        x += button_width + segment_gap;
    }
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
    for (std::size_t index = 0; index < state.tab_strips.size(); ++index) {
        set_ui_font(state.back_buttons[index], font);
        set_ui_font(state.forward_buttons[index], font);
        set_ui_font(state.up_buttons[index], font);
        set_ui_font(state.refresh_buttons[index], font);
        set_ui_font(state.view_mode_buttons[index], font);
        set_ui_font(state.address_bars[index], font);
        set_ui_font(state.status_bars[index], font);
    }
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

HFONT brand_font(HWND window) noexcept {
    HFONT base = ui_font(window);
    if (base == nullptr) return nullptr;
    LOGFONTW logfont{};
    if (GetObjectW(base, sizeof(logfont), &logfont) == 0) {
        DeleteObject(base);
        return nullptr;
    }
    DeleteObject(base);
    logfont.lfWeight = FW_BOLD;
    return CreateFontIndirectW(&logfont);
}

void draw_brand_bar(HWND window, HDC dc, RECT rect) noexcept {
    HBRUSH background = CreateSolidBrush(RGB(251, 252, 254));
    if (background != nullptr) {
        FillRect(dc, &rect, background);
        DeleteObject(background);
    }
    const int icon_size = scaled_value(window, 28);
    const int icon_margin = scaled_value(window, 14);
    const RECT icon{rect.left + icon_margin,
                    rect.top + ((rect.bottom - rect.top) - icon_size) / 2,
                    rect.left + icon_margin + icon_size,
                    rect.top + ((rect.bottom - rect.top) - icon_size) / 2 +
                        icon_size};
    const HICON app_icon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        icon_size, icon_size, LR_DEFAULTCOLOR));
    if (app_icon != nullptr) {
        DrawIconEx(dc, icon.left, icon.top, app_icon, icon_size, icon_size, 0,
                   nullptr, DI_NORMAL);
        DestroyIcon(app_icon);
    }

    RECT title{icon.right + scaled_value(window, 10), rect.top,
              rect.right - scaled_value(window, 8), rect.bottom};
    const HFONT font = brand_font(window);
    const HGDIOBJ old_font = font != nullptr ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(30, 41, 59));
    DrawTextW(dc, L"PaneDock", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (old_font != nullptr) SelectObject(dc, old_font);
    if (font != nullptr) DeleteObject(font);
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
    if (SetWindowRgn(container, region, TRUE) == 0) {
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
    HBRUSH shadow_brush = CreateSolidBrush(RGB(235, 239, 244));
    if (shadow_brush != nullptr) {
        const HGDIOBJ old_brush = SelectObject(dc, shadow_brush);
        const HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, shadow_rect.left, shadow_rect.top, shadow_rect.right,
                  shadow_rect.bottom, radius, radius);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(shadow_brush);
    }

    HBRUSH card_brush = CreateSolidBrush(RGB(255, 255, 255));
    if (card_brush != nullptr) {
        const HGDIOBJ old_brush = SelectObject(dc, card_brush);
        const HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, card.left, card.top, card.right, card.bottom, radius,
                  radius);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(card_brush);
    }

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
    HBRUSH fill = CreateSolidBrush(RGB(251, 252, 253));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(217, 225, 234));
    if (fill != nullptr && border != nullptr) {
        const HGDIOBJ old_brush = SelectObject(dc, fill);
        const HGDIOBJ old_pen = SelectObject(dc, border);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius,
                  radius);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
    }
    if (fill != nullptr) DeleteObject(fill);
    if (border != nullptr) DeleteObject(border);
}

void paint_client_background(HWND window, HDC dc,
                             const AppState& state) noexcept {
    const RECT client = client_rect(window);
    HBRUSH canvas = CreateSolidBrush(RGB(243, 246, 249));
    if (canvas != nullptr) {
        FillRect(dc, &client, canvas);
        DeleteObject(canvas);
    }

    const int sidebar_width = std::min(
        static_cast<int>(client.right - client.left),
        scaled_value(window, panedock::sidebar::kSidebarWidth));
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
        const auto rects = layout_rects(window, group);
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

void save_now(AppState& state, bool clean_shutdown = false) noexcept {
    capture_locations(state);
    state.session_document.application = state.application;
    state.session_document.clean_shutdown = clean_shutdown;
    if (!panedock::core::write_session(state.session_directory,
                                       state.session_document,
                                       flush_session_file)) {
        OutputDebugStringW(L"PaneDock: session persistence failed\n");
    }
}

void handle_navigation_complete(AppState& state, std::size_t pane_index,
                                std::wstring_view new_location) {
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size()) return;
    auto& tab = active_tab(active_group(state).panes[pane_index]);
    auto completed_location = location(std::wstring(new_location));
    if (state.suppress_history_record[pane_index]) {
        state.suppress_history_record[pane_index] = false;
        tab.location = std::move(completed_location);
        if (!tab.history.empty() && tab.history_index < tab.history.size()) {
            tab.history[tab.history_index] = tab.location;
        }
    } else {
        panedock::core::record_navigation(tab, std::move(completed_location));
    }
    apply_pane_view_mode(state, pane_index);
    refresh_tab_strip(state, pane_index);
    save_now(state);
}

void handle_navigation_failed(AppState& state, std::size_t pane_index) {
    if (pane_index >= kExplorerCount) return;
    // A pending back/forward navigation that fails asynchronously must still
    // release the suppression flag, or those buttons stay disabled forever.
    state.suppress_history_record[pane_index] = false;
    refresh_navigation_chrome(state, pane_index);
}

void destroy_explorers(AppState& state) noexcept {
    for (auto& explorer : state.explorers) {
        explorer.destroy();
    }
    state.realized.fill(false);
    write_live_view_count();
}

HRESULT apply_layout(HWND window, AppState& state) {
    layout_sidebar(window, state);
    layout_header(window, state);
    InvalidateRect(window, nullptr, TRUE);
    if (!has_active_group(state)) {
        state.laid_out_pane_rects.fill(std::nullopt);
        for (std::size_t index = 0; index < state.explorers.size(); ++index) {
            state.explorers[index].set_visible(false);
            ShowWindow(state.explorer_containers[index], SW_HIDE);
            ShowWindow(state.tab_strips[index], SW_HIDE);
            ShowWindow(state.back_buttons[index], SW_HIDE);
            ShowWindow(state.forward_buttons[index], SW_HIDE);
            ShowWindow(state.up_buttons[index], SW_HIDE);
            ShowWindow(state.refresh_buttons[index], SW_HIDE);
            ShowWindow(state.view_mode_buttons[index], SW_HIDE);
            ShowWindow(state.address_bars[index], SW_HIDE);
            ShowWindow(state.status_bars[index], SW_HIDE);
        }
        ShowWindow(state.empty_message, SW_SHOW);
        write_live_view_count();
        return S_OK;
    }
    ShowWindow(state.empty_message, SW_HIDE);
    auto& group = active_group(state);
    const auto rects = layout_rects(window, group);
    const UINT dpi = GetDpiForWindow(window);
    const int container_radius = pane_card_radius(dpi);
    for (std::size_t index = 0; index < state.explorers.size(); ++index) {
        const bool visible = index < group.panes.size();
        RECT pane_rect{};
        bool pane_geometry_changed = false;
        if (visible) {
            pane_rect = to_win32_rect(rects[index]);
            pane_geometry_changed =
                !state.laid_out_pane_rects[index].has_value() ||
                !EqualRect(&state.laid_out_pane_rects[index].value(),
                           &pane_rect);
            const int strip_height = scaled_value(window, kTabStripHeight);
            const int actual_strip_height =
                std::min(strip_height, static_cast<int>(pane_rect.bottom -
                                                        pane_rect.top));
            SetWindowPos(state.tab_strips[index], nullptr, pane_rect.left,
                         pane_rect.top, pane_rect.right - pane_rect.left,
                         actual_strip_height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(state.tab_strips[index], SW_SHOW);
            apply_tab_item_size(state, index);

            const NavigationGeometry geometry =
                navigation_geometry(window, pane_rect);
            const int navigation_top = geometry.navigation_top;
            const int navigation_height = geometry.navigation_height;
            const std::array<HWND, 5> buttons{
                state.back_buttons[index], state.forward_buttons[index],
                state.up_buttons[index], state.refresh_buttons[index],
                state.view_mode_buttons[index]};
            int x = pane_rect.left;
            for (HWND button : buttons) {
                SetWindowPos(button, nullptr, x, navigation_top,
                             geometry.button_width, navigation_height,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                ShowWindow(button, SW_SHOW);
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
            SetWindowPos(state.address_bars[index], nullptr, address_rect.left,
                         address_rect.top,
                         address_rect.right - address_rect.left,
                         address_rect.bottom - address_rect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(state.address_bars[index], SW_SHOW);

            RECT rect = pane_rect;
            rect.top = navigation_top + navigation_height;
            const int status_height = std::min(
                scaled_value(window, kStatusBarHeight),
                std::max(0, static_cast<int>(rect.bottom - rect.top)));
            const RECT status_rect{rect.left, rect.bottom - status_height,
                                   rect.right, rect.bottom};
            SetWindowPos(state.status_bars[index], nullptr, status_rect.left,
                         status_rect.top, status_rect.right - status_rect.left,
                         status_rect.bottom - status_rect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(state.status_bars[index], SW_SHOW);
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
            SetWindowPos(state.explorer_containers[index], nullptr, rect.left,
                         rect.top, container_width, container_height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(state.explorer_containers[index], SW_SHOW);
            apply_pane_container_region(state.explorer_containers[index],
                                        container_width, container_height,
                                        container_radius);
            const RECT local_rect{0, 0, container_width, container_height};
            if (!state.realized[index]) {
                const HRESULT hr = state.explorers[index].initialize(
                    state.explorer_containers[index], local_rect,
                    active_tab(group.panes[index]).location.parsing_name);
                if (FAILED(hr)) {
                    return hr;
                }
                state.realized[index] = true;
                try {
                    state.explorers[index].set_navigation_callback(
                        [&state, index](std::wstring_view new_location) {
                            handle_navigation_complete(state, index,
                                                       new_location);
                        });
                    state.explorers[index].set_navigation_failed_callback(
                        [&state, index]() {
                            handle_navigation_failed(state, index);
                        });
                    state.explorers[index].set_selection_changed_callback(
                        [&state, index]() { refresh_status_bar(state, index); });
                    apply_pane_view_mode(state, index);
                } catch (...) {
                    return E_OUTOFMEMORY;
                }
            } else {
                state.explorers[index].set_rect(local_rect);
            }
            refresh_status_bar(state, index);
            state.laid_out_pane_rects[index] = pane_rect;
        } else {
            state.laid_out_pane_rects[index].reset();
            ShowWindow(state.explorer_containers[index], SW_HIDE);
            ShowWindow(state.tab_strips[index], SW_HIDE);
            ShowWindow(state.back_buttons[index], SW_HIDE);
            ShowWindow(state.forward_buttons[index], SW_HIDE);
            ShowWindow(state.up_buttons[index], SW_HIDE);
            ShowWindow(state.refresh_buttons[index], SW_HIDE);
            ShowWindow(state.view_mode_buttons[index], SW_HIDE);
            ShowWindow(state.address_bars[index], SW_HIDE);
            ShowWindow(state.status_bars[index], SW_HIDE);
        }
        state.explorers[index].set_visible(visible);
        if (visible && pane_geometry_changed) {
            RedrawWindow(window, &pane_rect, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        }
    }
    write_live_view_count();
    return S_OK;
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
                                      location(kDefaultLocations.front()));
    }
    for (std::size_t index = 0; index < group.panes.size(); ++index)
        active_tab(group.panes[index]).location = location(kDefaultLocations[index]);
    return group;
}

void activate_group(HWND window, AppState& state, std::size_t index) {
    if (index >= state.application.groups.size()) return;
    const std::string target_id = state.application.groups[index].id;
    if (target_id == state.application.active_group_id) {
        refresh_sidebar(state);
        return;
    }

    capture_locations(state);
    state.application.active_group_id = target_id;
    refresh_tab_strips(state);
    auto& group = active_group(state);
    for (std::size_t pane = 0; pane < group.panes.size(); ++pane) {
        if (state.realized[pane]) {
            state.explorers[pane].navigate(
                active_tab(group.panes[pane]).location.parsing_name);
        }
    }
    if (FAILED(apply_layout(window, state)))
        OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
    state.explorers[active_pane_index(group)].focus();
    refresh_sidebar(state);
    save_now(state);
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
                                  std::move(hover_callback));
}

void add_group(HWND window, AppState& state) {
    const bool was_empty = state.application.groups.empty();
    const std::string id = unique_group_id(state.application);
    if (!panedock::core::add_group(state.application,
                                   new_group_state(state, id))) return;
    if (was_empty) {
        refresh_tab_strips(state);
        if (FAILED(apply_layout(window, state)))
            OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
        state.explorers[active_pane_index(active_group(state))].focus();
        refresh_sidebar(state);
        save_now(state);
        return;
    }
    activate_group(window, state, state.application.groups.size() - 1);
}

void duplicate_group(HWND window, AppState& state) {
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

    capture_locations(state);
    const std::string id = state.application.groups[*selected].id;
    const bool deleted_active = id == state.application.active_group_id;
    if (!panedock::core::delete_group(state.application, id)) return;
    refresh_tab_strips(state);
    if (deleted_active && has_active_group(state)) {
        auto& group = active_group(state);
        for (std::size_t pane = 0; pane < group.panes.size(); ++pane) {
            if (state.realized[pane])
                state.explorers[pane].navigate(
                    active_tab(group.panes[pane]).location.parsing_name);
        }
    }
    if (FAILED(apply_layout(window, state)))
        OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
    if (has_active_group(state)) {
        state.explorers[active_pane_index(active_group(state))].focus();
    }
    refresh_sidebar(state);
    save_now(state);
}

void move_group(AppState& state, bool down) {
    const auto selected = state.sidebar.selected_index();
    if (!selected.has_value() || *selected >= state.application.groups.size()) return;
    if ((!down && *selected == 0) ||
        (down && *selected + 1 >= state.application.groups.size())) return;
    const std::string id = state.application.groups[*selected].id;
    const std::size_t target = down ? *selected + 1 : *selected - 1;
    if (!panedock::core::reorder_group(state.application, id, target)) return;
    refresh_sidebar(state);
    save_now(state);
}

void set_active_pane(HWND window, AppState& state, std::size_t pane) noexcept {
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    if (pane >= group.panes.size()) return;
    const std::size_t previous = active_pane_index(group);
    if (previous == pane ||
        !panedock::core::set_active_pane(group, group.panes[pane].id)) return;
    state.explorers[pane].focus();
    InvalidateRect(state.tab_strips[previous], nullptr, FALSE);
    InvalidateRect(state.tab_strips[pane], nullptr, FALSE);
    InvalidateRect(window, nullptr, TRUE);
    save_now(state);
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
    if (state.realized[pane_index]) {
        state.explorers[pane_index].navigate(
            active_tab(pane).location.parsing_name);
    }
    refresh_tab_strip(state, pane_index);
    save_now(state);
}

std::optional<std::size_t> tab_item_at_point(const AppState& state, HWND strip,
                                             POINT point) noexcept;

bool register_tab_drag_hover_targets(HWND window, AppState& state) {
    for (std::size_t pane_index = 0;
         pane_index < state.tab_strips.size(); ++pane_index) {
        const HWND strip = state.tab_strips[pane_index];
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
            std::move(hit_test), std::move(hover_callback));
        if (target == nullptr || FAILED(RegisterDragDrop(strip, target.Get())))
            return false;
        state.tab_drag_targets[pane_index] = std::move(target);
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

void add_tab_to_pane(HWND, AppState& state, std::size_t pane_index) {
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    if (pane_index >= group.panes.size()) return;
    capture_pane_location(state, pane_index);
    std::size_t candidate = 0;
    const std::string id = unique_tab_id(group, candidate);
    auto& pane = group.panes[pane_index];
    if (!panedock::core::add_tab(
            pane, {id, location(kDefaultLocations[pane_index]), {}, {}, true,
                   {}, 0}) ||
        !panedock::core::set_active_tab(pane, id)) return;
    if (state.realized[pane_index]) {
        state.explorers[pane_index].navigate(
            active_tab(pane).location.parsing_name);
    }
    refresh_tab_strip(state, pane_index);
    save_now(state);
}

void close_tab_in_pane(HWND, AppState& state, std::size_t pane_index,
                       const std::string& tab_id) {
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    if (pane_index >= group.panes.size()) return;
    auto& pane = group.panes[pane_index];
    capture_pane_location(state, pane_index);
    const bool closed_active = pane.active_tab_id == tab_id;
    if (!panedock::core::close_tab(
            pane, tab_id, location(kDefaultLocations[pane_index]))) return;
    if (closed_active && state.realized[pane_index]) {
        state.explorers[pane_index].navigate(
            active_tab(pane).location.parsing_name);
    }
    refresh_tab_strip(state, pane_index);
    save_now(state);
}

void navigate_tab_history(AppState& state, std::size_t pane_index, bool back) {
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size() ||
        state.suppress_history_record[pane_index]) return;
    auto& tab = active_tab(active_group(state).panes[pane_index]);
    const bool moved = back ? panedock::core::navigate_tab_back(tab)
                            : panedock::core::navigate_tab_forward(tab);
    if (!moved) return;
    state.suppress_history_record[pane_index] = true;
    const HRESULT hr = state.explorers[pane_index].navigate(
        tab.location.parsing_name);
    if (FAILED(hr)) state.suppress_history_record[pane_index] = false;
    refresh_navigation_chrome(state, pane_index);
}

void navigate_up(AppState& state, std::size_t pane_index) {
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size()) return;
    state.explorers[pane_index].navigate_up();
}

void refresh_pane(AppState& state, std::size_t pane_index) {
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size())
        return;
    (void)state.explorers[pane_index].refresh();
}

void set_pane_view_mode(AppState& state, std::size_t pane_index,
                        const ViewModeOption& option) {
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size())
        return;
    if (SUCCEEDED(state.explorers[pane_index].set_view_mode(
            option.mode, option.image_size))) {
        active_tab(active_group(state).panes[pane_index]).view_mode =
            view_mode_name(option.mode, option.image_size);
        save_now(state);
    }
}

void show_view_mode_menu(HWND window, AppState& state,
                         std::size_t pane_index) {
    if (!has_active_group(state) || pane_index >= active_group(state).panes.size())
        return;

    RECT button_rect{};
    if (!GetWindowRect(state.view_mode_buttons[pane_index], &button_rect))
        return;

    const auto current = parse_view_mode(
        active_tab(active_group(state).panes[pane_index]).view_mode);
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    const int menu_id_base =
        kViewModeMenuIdBase +
        static_cast<int>(pane_index * kViewModeOptions.size());
    int checked_id = 0;
    for (std::size_t index = 0; index < kViewModeOptions.size(); ++index) {
        const bool checked = current.has_value() &&
                             current->mode == kViewModeOptions[index].mode &&
                             current->image_size ==
                                 kViewModeOptions[index].image_size;
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
    if (command != 0)
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(command, 0), 0);
}

void submit_address(AppState& state, std::size_t pane_index) {
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size()) return;
    const HWND edit = state.address_bars[pane_index];
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(edit, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    state.explorers[pane_index].navigate(text);
}

LRESULT CALLBACK address_edit_proc(HWND window, UINT message, WPARAM wparam,
                                   LPARAM lparam, UINT_PTR pane_index,
                                   DWORD_PTR reference_data) {
    auto* state = reinterpret_cast<AppState*>(reference_data);
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
                                       location(kDefaultLocations.front()),
                                       pane_ids, tab_ids)) return;
    for (std::size_t index = target_count - pane_ids.size();
         index < target_count; ++index) {
        active_tab(group.panes[index]).location =
            location(kDefaultLocations[index]);
    }
    refresh_tab_strips(state);
    if (FAILED(apply_layout(window, state))) {
        OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
    }

    const std::size_t active = active_pane_index(group);
    state.explorers[active].focus();
    save_now(state);
}

void update_splitter_drag(HWND window, AppState& state, POINT point) {
    if (!state.splitter_drag.has_value()) return;
    auto& group = active_group(state);
    const Splitter& drag = *state.splitter_drag;
    if (drag.ratio_index >= group.divider_ratios.size()) return;

    const RECT client = pane_content_area(window);
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
    if (FAILED(apply_layout(window, state)))
        OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
}

std::size_t pane_at_point(HWND window, const AppState& state,
                          POINT point) noexcept {
    if (!has_active_group(state)) return kExplorerCount;
    const auto rects = layout_rects(window, active_group(state));
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
    const auto found = std::find(state.tab_strips.begin(),
                                 state.tab_strips.end(), strip);
    if (found == state.tab_strips.end()) return std::nullopt;
    return static_cast<std::size_t>(found - state.tab_strips.begin());
}

bool address_bar_has_focus(const AppState& state) noexcept {
    const HWND focused = GetFocus();
    return std::find(state.address_bars.begin(), state.address_bars.end(),
                     focused) != state.address_bars.end();
}

void close_tab_at_point(HWND window, AppState& state, POINT point) {
    const std::size_t pane_index = pane_at_point(window, state, point);
    if (pane_index >= state.tab_strips.size() ||
        pane_index >= active_group(state).panes.size()) return;
    POINT client = point;
    MapWindowPoints(window, state.tab_strips[pane_index], &client, 1);
    const auto item = tab_item_at_point(
        state, state.tab_strips[pane_index], client);
    const auto& tabs = active_group(state).panes[pane_index].tabs;
    if (!item.has_value() || *item >= tabs.size()) return;
    const std::string id = tabs[*item].id;
    close_tab_in_pane(window, state, pane_index, id);
}

RECT tab_viewport_rect(const AppState& state, std::size_t pane_index) noexcept {
    const RECT add = state.tab_add_rects[pane_index];
    const RECT back = state.tab_scroll_button_rects[pane_index][0];
    const int right = back.right > back.left ? back.left : add.left;
    return {0, 0, right, add.bottom};
}

std::optional<std::size_t> tab_item_at_point(const AppState& state, HWND strip,
                                             POINT point) noexcept {
    const auto pane_index = tab_strip_index(state, strip);
    if (!pane_index.has_value() || !has_active_group(state) ||
        *pane_index >= active_group(state).panes.size()) {
        return std::nullopt;
    }
    const RECT viewport = tab_viewport_rect(state, *pane_index);
    if (!PtInRect(&viewport, point)) return std::nullopt;
    if (state.tab_drag.has_value() && state.tab_drag->strip == strip &&
        state.tab_drag->target_index.has_value() &&
        state.tab_placeholder_rects[*pane_index].has_value() &&
        PtInRect(&*state.tab_placeholder_rects[*pane_index], point))
        return state.tab_drag->target_index;
    const auto& visuals = state.tab_visuals[*pane_index];
    for (std::size_t index = 0; index < visuals.size(); ++index)
        if (PtInRect(&visuals[index].rect, point)) return index;
    return std::nullopt;
}

int tab_scroll_step(const AppState& state, std::size_t pane_index,
                    bool forward) noexcept {
    const RECT viewport = tab_viewport_rect(state, pane_index);
    const auto& visuals = state.tab_visuals[pane_index];
    auto width_if_visible = [&viewport](const AppState::TabVisual& visual) {
        if (visual.rect.right <= visual.rect.left ||
            visual.rect.right <= viewport.left ||
            visual.rect.left >= viewport.right) return 0;
        return static_cast<int>(visual.rect.right - visual.rect.left);
    };
    int edge = forward ? std::numeric_limits<int>::max()
                       : std::numeric_limits<int>::min();
    int step = 0;
    for (const auto& visual : visuals) {
        const int width = width_if_visible(visual);
        if (width <= 0) continue;
        const int candidate = forward ? visual.rect.left : visual.rect.right;
        if ((forward && candidate < edge) || (!forward && candidate > edge)) {
            edge = candidate;
            step = width;
        }
    }
    return step;
}

void scroll_tab_strip(AppState& state, std::size_t pane_index, bool forward) {
    if (pane_index >= state.tab_scroll_offsets.size() ||
        state.tab_scroll_max_offsets[pane_index] <= 0) return;
    const int offset = state.tab_scroll_offsets[pane_index];
    const int maximum = state.tab_scroll_max_offsets[pane_index];
    if ((!forward && offset <= 0) || (forward && offset >= maximum)) return;
    const int step = tab_scroll_step(state, pane_index, forward);
    if (step <= 0) return;
    state.tab_scroll_offsets[pane_index] =
        offset + (forward ? step : -step);
    apply_tab_item_size(state, pane_index);
}

void cancel_tab_drag(AppState& state, HWND strip) noexcept {
    if (!state.tab_drag.has_value() || state.tab_drag->strip != strip)
        return;
    state.tab_drag.reset();
    if (const auto pane = tab_strip_index(state, strip); pane.has_value())
        apply_tab_item_size(state, *pane);
    if (GetCapture() == strip) ReleaseCapture();
}

void finish_tab_drag(AppState& state, HWND strip) {
    if (!state.tab_drag.has_value() || state.tab_drag->strip != strip)
        return;
    AppState::TabDrag drag = std::move(*state.tab_drag);
    state.tab_drag.reset();
    apply_tab_item_size(state, drag.pane_index);
    if (GetCapture() == strip) ReleaseCapture();
    if (!drag.dragging || !drag.target_index.has_value() ||
        *drag.target_index == drag.source_index || !has_active_group(state))
        return;
    auto& group = active_group(state);
    if (drag.pane_index >= group.panes.size()) return;
    auto& pane = group.panes[drag.pane_index];
    if (panedock::core::reorder_tab(pane, drag.tab_id,
                                    *drag.target_index)) {
        refresh_tab_strip(state, drag.pane_index);
        save_now(state);
    }
}

void update_tab_drag(AppState& state, HWND strip, WPARAM wparam,
                     LPARAM lparam) {
    if (!state.tab_drag.has_value() || state.tab_drag->strip != strip) return;
    if ((wparam & MK_LBUTTON) == 0) {
        cancel_tab_drag(state, strip);
        return;
    }
    const POINT point = point_from_lparam(lparam);
    RECT client{};
    GetClientRect(strip, &client);
    if (!PtInRect(&client, point)) {
        cancel_tab_drag(state, strip);
        return;
    }
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
    const auto target = tab_item_at_point(state, strip, point);
    if (target == state.tab_drag->target_index) return;
    state.tab_drag->target_index = target;
    apply_tab_item_size(state, state.tab_drag->pane_index);
}

void draw_tab_scroll_button(HWND window, HDC dc, const RECT& rect,
                            bool forward, bool disabled) noexcept {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    const int visual_width = std::min(
        scaled_value(window, kTabScrollButtonVisualWidth), width);
    const int visual_height = std::min(
        scaled_value(window, kTabScrollButtonVisualHeight), height);
    const int visual_top = rect.top + (height - visual_height) / 2;
    // The two hit-test rects are adjacent. Inset them toward their shared edge
    // so the compact visual buttons stay together in the middle.
    const int visual_left = forward ? rect.left : rect.right - visual_width;
    const RECT visual{visual_left, visual_top, visual_left + visual_width,
                      visual_top + visual_height};
    HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
    HPEN border = CreatePen(PS_SOLID, scaled_value(window, 1),
                            RGB(226, 232, 240));
    if (background != nullptr && border != nullptr) {
        const HGDIOBJ old_brush = SelectObject(dc, background);
        const HGDIOBJ old_pen = SelectObject(dc, border);
        const int radius = std::min(
            scaled_value(window, kTabScrollButtonCornerRadius),
            std::min(visual_width, visual_height) / 2);
        RoundRect(dc, visual.left, visual.top, visual.right, visual.bottom,
                  radius, radius);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
    }
    if (background != nullptr) {
        DeleteObject(background);
    }
    if (border != nullptr) DeleteObject(border);
    const int half = std::max(
        2, std::min(scaled_value(window, kTabScrollButtonGlyphHalf),
                    std::min(visual_width, visual_height) / 2));
    const int center_x = (visual.left + visual.right) / 2;
    const int center_y = (visual.top + visual.bottom) / 2;
    const int direction = forward ? 1 : -1;
    const COLORREF color =
        disabled ? RGB(190, 197, 209) : RGB(90, 102, 122);
    HPEN pen = CreatePen(PS_SOLID, scaled_value(window, 1), color);
    if (pen == nullptr) return;
    const HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, center_x - direction * half, center_y - half, nullptr);
    LineTo(dc, center_x + direction * half, center_y);
    LineTo(dc, center_x - direction * half, center_y + half);
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
    const auto& visuals = state.tab_visuals[pane_index];
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
        RECT rect = visuals[index].rect;
        rect.left = std::min(rect.right, rect.left + left_gap);
        rect.right = std::max(rect.left, rect.right - right_gap);
        rect.top = std::min(rect.bottom, rect.top + vertical_padding);
        rect.bottom = std::max(rect.top, rect.bottom - vertical_padding);
        const bool active = pane.tabs[index].id == pane.active_tab_id;
        const bool hovered = !active &&
                             state.tab_hover_indices[pane_index] == index;
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
    if (state.tab_placeholder_rects[pane_index].has_value()) {
        RECT rect = *state.tab_placeholder_rects[pane_index];
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
        if (state.tab_drag.has_value() && state.tab_drag->dragging &&
            state.tab_drag->pane_index == pane_index &&
            state.tab_drag->source_index < visuals.size()) {
            RECT text_rect = rect;
            text_rect.left = std::min(text_rect.right,
                                      text_rect.left + text_padding);
            text_rect.right = std::max(text_rect.left,
                                       text_rect.right - text_padding);
            SetTextColor(dc, panedock::sidebar::kPlaceholderContent);
            DrawTextW(dc,
                      visuals[state.tab_drag->source_index].text.c_str(), -1,
                      &text_rect,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
                          DT_NOPREFIX);
        }
    }
    if (saved_dc != 0) RestoreDC(dc, saved_dc);
    const auto& scroll_buttons = state.tab_scroll_button_rects[pane_index];
    if (scroll_buttons[0].right > scroll_buttons[0].left) {
        draw_tab_scroll_button(
            window, dc, scroll_buttons[0], false,
            state.tab_scroll_offsets[pane_index] <= 0);
        draw_tab_scroll_button(
            window, dc, scroll_buttons[1], true,
            state.tab_scroll_offsets[pane_index] >=
                state.tab_scroll_max_offsets[pane_index]);
    }
    RECT add = state.tab_add_rects[pane_index];
    RECT hover = add;
    const int add_inset = scaled_value(window, 5);
    InflateRect(&hover, -add_inset, -add_inset);
    if (state.tab_hover_indices[pane_index].has_value() &&
        *state.tab_hover_indices[pane_index] == pane.tabs.size()) {
        HBRUSH fill = CreateSolidBrush(RGB(236, 240, 244));
        if (fill != nullptr) {
            FillRect(dc, &hover, fill);
            DeleteObject(fill);
        }
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
        SetTextColor(dc, RGB(31, 41, 55));
        RECT plus_rect = add;
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
    if (state != nullptr && pane_index < state->tab_strips.size()) {
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
            const auto& scroll_buttons =
                state->tab_scroll_button_rects[pane_index];
            if (PtInRect(&scroll_buttons[0], point)) {
                scroll_tab_strip(*state, pane_index, false);
                return 0;
            }
            if (PtInRect(&scroll_buttons[1], point)) {
                scroll_tab_strip(*state, pane_index, true);
                return 0;
            }
            const auto item = tab_item_at_point(*state, window, point);
            if (item.has_value()) {
                const auto& tabs = active_group(*state).panes[pane_index].tabs;
                if (*item >= tabs.size()) return 0;
                if (state->tab_drag.has_value())
                    cancel_tab_drag(*state, state->tab_drag->strip);
                state->tab_drag = AppState::TabDrag{
                    window, pane_index, *item, tabs[*item].id, point, false,
                    std::nullopt};
                SetCapture(window);
                SendMessageW(GetParent(window), kTabStripSelectionMessage,
                             static_cast<WPARAM>(pane_index),
                             static_cast<LPARAM>(*item));
            } else if (PtInRect(&state->tab_add_rects[pane_index], point)) {
                SendMessageW(GetParent(window), kTabStripSelectionMessage,
                             static_cast<WPARAM>(pane_index), -1);
            }
            return 0;
        }
        if (message == WM_MOUSEMOVE) {
            const POINT point = point_from_lparam(lparam);
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            std::optional<std::size_t> hover =
                tab_item_at_point(*state, window, point);
            if (!hover.has_value() && has_active_group(*state) &&
                pane_index < active_group(*state).panes.size() &&
                PtInRect(&state->tab_add_rects[pane_index], point)) {
                hover = active_group(*state).panes[pane_index].tabs.size();
            }
            if (state->tab_hover_indices[pane_index] != hover) {
                state->tab_hover_indices[pane_index] = hover;
                InvalidateRect(window, nullptr, FALSE);
            }
            update_tab_drag(*state, window, wparam, lparam);
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            if (state->tab_hover_indices[pane_index].has_value()) {
                state->tab_hover_indices[pane_index].reset();
                InvalidateRect(window, nullptr, FALSE);
            }
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

LRESULT CALLBACK layout_button_proc(HWND window, UINT message, WPARAM wparam,
                                    LPARAM lparam, UINT_PTR button_index,
                                    DWORD_PTR reference_data) {
    auto* state = reinterpret_cast<AppState*>(reference_data);
    if (state != nullptr && button_index < state->layout_buttons.size()) {
        if (message == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            if (state->layout_hover_index != button_index) {
                state->layout_hover_index = button_index;
                InvalidateRect(window, nullptr, FALSE);
            }
        } else if (message == WM_MOUSELEAVE) {
            if (state->layout_hover_index == button_index) {
                state->layout_hover_index.reset();
                InvalidateRect(window, nullptr, FALSE);
            }
        } else if (message == WM_NCDESTROY) {
            if (state->layout_hover_index == button_index)
                state->layout_hover_index.reset();
            RemoveWindowSubclass(window, layout_button_proc, button_index);
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
        save_now(state);
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

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam,
                             LPARAM lparam) {
    auto* state = reinterpret_cast<AppState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<AppState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX controls{
                sizeof(controls), ICC_TAB_CLASSES | ICC_WIN95_CLASSES};
            if (!InitCommonControlsEx(&controls)) return -1;
            state->sidebar_drag_target =
                make_sidebar_drag_hover_target(window, *state);
            if (!state->sidebar.create(window, kGroupListId,
                                       state->sidebar_drag_target.Get())) {
                state->sidebar_drag_target.Reset();
                return -1;
            }
            if (!SetWindowSubclass(state->sidebar.window(), group_list_proc, 0,
                                   reinterpret_cast<DWORD_PTR>(state))) {
                state->sidebar.revoke_drag_drop();
                state->sidebar_drag_target.Reset();
                return -1;
            }
            state->group_label = CreateWindowExW(
                0, L"STATIC", L"GROUPS",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0,
                window, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (state->group_label == nullptr) return -1;
            for (std::size_t index = 0; index < state->sidebar_buttons.size();
                 ++index) {
                state->sidebar_buttons[index] = CreateWindowExW(
                    0, L"BUTTON", kButtonLabels[index],
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON |
                        BS_OWNERDRAW,
                    0, 0, 0, 0, window,
                    reinterpret_cast<HMENU>(kButtonIds[index]),
                    GetModuleHandleW(nullptr), nullptr);
                if (state->sidebar_buttons[index] == nullptr) return -1;
            }
            for (std::size_t index = 0; index < state->layout_buttons.size();
                 ++index) {
                state->layout_buttons[index] = CreateWindowExW(
                    0, L"BUTTON", kLayoutButtonLabels[index],
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON |
                        BS_OWNERDRAW | (index == 0 ? WS_GROUP : 0),
                    0, 0, 0, 0, window,
                    reinterpret_cast<HMENU>(kLayoutButtonIds[index]),
                    GetModuleHandleW(nullptr), nullptr);
                if (state->layout_buttons[index] == nullptr) return -1;
                if (!SetWindowSubclass(state->layout_buttons[index],
                                       layout_button_proc, index,
                                       reinterpret_cast<DWORD_PTR>(state)))
                    return -1;
            }
            state->layout_tooltip = CreateWindowExW(
                WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
                CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr,
                GetModuleHandleW(nullptr), nullptr);
            if (state->layout_tooltip != nullptr) {
                constexpr std::array<const wchar_t*, 5> kLayoutTooltips{
                    L"Single pane", L"Two panes side by side",
                    L"Two panes stacked", L"Three panes", L"Four panes"};
                for (std::size_t index = 0;
                     index < state->layout_buttons.size(); ++index) {
                    TOOLINFOW info{};
                    info.cbSize = sizeof(info);
                    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                    info.hwnd = window;
                    info.uId = reinterpret_cast<UINT_PTR>(
                        state->layout_buttons[index]);
                    info.lpszText = const_cast<wchar_t*>(kLayoutTooltips[index]);
                    SendMessageW(state->layout_tooltip, TTM_ADDTOOLW, 0,
                                  reinterpret_cast<LPARAM>(&info));
                }
            }
            state->empty_message = CreateWindowExW(
                0, L"STATIC", L"No Group. Click New Group to get started.",
                WS_CHILD | SS_CENTER | SS_CENTERIMAGE, 0, 0, 0, 0, window,
                nullptr, GetModuleHandleW(nullptr), nullptr);
            if (state->empty_message == nullptr) return -1;
            for (std::size_t index = 0; index < state->tab_strips.size();
                 ++index) {
                // PD-040: plain STATIC child used purely as a clipping
                // container (SetWindowRgn) and a parent HWND for
                // ExplorerHost::initialize — it never paints or handles
                // messages of its own, so no custom window class is needed.
                state->explorer_containers[index] = CreateWindowExW(
                    0, L"STATIC", nullptr,
                    WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                    window, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (state->explorer_containers[index] == nullptr) return -1;
                state->tab_strips[index] = CreateWindowExW(
                    0, L"STATIC", nullptr,
                    WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | SS_NOTIFY,
                    0, 0, 0, 0, window,
                    reinterpret_cast<HMENU>(kTabStripIdBase +
                                             static_cast<int>(index)),
                    GetModuleHandleW(nullptr), nullptr);
                if (state->tab_strips[index] == nullptr) return -1;
                if (!SetWindowSubclass(state->tab_strips[index], tab_strip_proc,
                                       index,
                                       reinterpret_cast<DWORD_PTR>(state)))
                    return -1;
                const std::array<const wchar_t*, 5> labels{
                    L"<", L">", L"Up", L"Refresh", L"View"};
                const std::array<int, 5> ids{
                    kBackButtonIdBase + static_cast<int>(index),
                    kForwardButtonIdBase + static_cast<int>(index),
                    kUpButtonIdBase + static_cast<int>(index),
                    kRefreshButtonIdBase + static_cast<int>(index),
                    kViewModeButtonIdBase + static_cast<int>(index)};
                const std::array<HWND*, 5> destinations{
                    &state->back_buttons[index], &state->forward_buttons[index],
                    &state->up_buttons[index], &state->refresh_buttons[index],
                    &state->view_mode_buttons[index]};
                for (std::size_t button = 0; button < labels.size(); ++button) {
                    *destinations[button] = CreateWindowExW(
                        0, L"BUTTON", labels[button],
                        WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP |
                            BS_PUSHBUTTON | BS_OWNERDRAW,
                        0, 0, 0, 0,
                        window, reinterpret_cast<HMENU>(ids[button]),
                        GetModuleHandleW(nullptr), nullptr);
                    if (*destinations[button] == nullptr) return -1;
                }
                state->address_bars[index] = CreateWindowExW(
                    0, L"EDIT", nullptr,
                    WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL,
                    0, 0, 0, 0,
                    window,
                    reinterpret_cast<HMENU>(kAddressBarIdBase +
                                             static_cast<int>(index)),
                    GetModuleHandleW(nullptr), nullptr);
                if (state->address_bars[index] == nullptr ||
                    !SetWindowSubclass(state->address_bars[index],
                                       address_edit_proc, index,
                                       reinterpret_cast<DWORD_PTR>(state)))
                    return -1;
                (void)SHAutoComplete(state->address_bars[index],
                                     SHACF_FILESYS_DIRS);
                state->status_bars[index] = CreateWindowExW(
                    0, L"STATIC", L"",
                    WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW,
                    0, 0, 0, 0, window, nullptr, GetModuleHandleW(nullptr),
                    nullptr);
                if (state->status_bars[index] == nullptr) return -1;
            }
            refresh_ui_font(window, *state);
            refresh_sidebar(*state);
            refresh_tab_strips(*state);
            if (FAILED(apply_layout(window, *state))) {
                MessageBoxW(window, L"PaneDock could not open the Shell view.",
                            L"PaneDock", MB_ICONERROR | MB_OK);
                revoke_drag_hover_targets(*state);
                destroy_explorers(*state);
                return -1;
            }
            if (has_active_group(*state)) {
                const std::size_t active =
                    active_pane_index(active_group(*state));
                state->explorers[active].focus();
            }
            if (!register_tab_drag_hover_targets(window, *state)) {
                OutputDebugStringW(
                    L"PaneDock: RegisterDragDrop for tab strip failed\n");
                revoke_drag_hover_targets(*state);
                destroy_explorers(*state);
                return -1;
            }
            return 0;
        }
        case kTabStripSelectionMessage:
            if (state != nullptr && has_active_group(*state) &&
                wparam < state->tab_strips.size() &&
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
        case WM_DRAWITEM:
            if (state != nullptr) {
                const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
                if (item != nullptr &&
                    std::find(state->status_bars.begin(),
                              state->status_bars.end(),
                              item->hwndItem) != state->status_bars.end()) {
                    draw_status_bar(*item, GetDpiForWindow(item->hwndItem));
                    return TRUE;
                }
                if (item != nullptr && item->CtlType == ODT_BUTTON &&
                    item->CtlID >= kLayoutButtonIdBase &&
                    item->CtlID < kLayoutButtonIdBase +
                                      static_cast<int>(kLayoutButtonIds.size())) {
                    const std::size_t index = static_cast<std::size_t>(
                        item->CtlID - kLayoutButtonIdBase);
                    const bool enabled = has_active_group(*state);
                    const auto current =
                        enabled ? active_group(*state).layout_template
                                : panedock::core::LayoutTemplate::single;
                    draw_layout_button(*item, index,
                                       kLayoutTemplates[index] == current,
                                       state->layout_hover_index == index);
                    return TRUE;
                }
                if (item != nullptr && item->CtlType == ODT_BUTTON &&
                    item->CtlID >= kBackButtonIdBase &&
                    item->CtlID <
                        kBackButtonIdBase + static_cast<int>(kExplorerCount)) {
                    draw_navigation_icon_button(*item, 0);
                    return TRUE;
                }
                if (item != nullptr && item->CtlType == ODT_BUTTON &&
                    item->CtlID >= kForwardButtonIdBase &&
                    item->CtlID < kForwardButtonIdBase +
                                      static_cast<int>(kExplorerCount)) {
                    draw_navigation_icon_button(*item, 1);
                    return TRUE;
                }
                if (item != nullptr && item->CtlType == ODT_BUTTON &&
                    item->CtlID >= kUpButtonIdBase &&
                    item->CtlID <
                        kUpButtonIdBase + static_cast<int>(kExplorerCount)) {
                    draw_navigation_icon_button(*item, 2);
                    return TRUE;
                }
                if (item != nullptr && item->CtlType == ODT_BUTTON &&
                    item->CtlID >= kRefreshButtonIdBase &&
                    item->CtlID < kRefreshButtonIdBase +
                                      static_cast<int>(kExplorerCount)) {
                    draw_navigation_icon_button(*item, 3);
                    return TRUE;
                }
                if (item != nullptr && item->CtlType == ODT_BUTTON &&
                    item->CtlID >= kViewModeButtonIdBase &&
                    item->CtlID < kViewModeButtonIdBase +
                                      static_cast<int>(kExplorerCount)) {
                    draw_navigation_icon_button(*item, 4);
                    return TRUE;
                }
                if (item != nullptr && item->CtlType == ODT_BUTTON) {
                    const auto found = std::find(
                        kButtonIds.begin(), kButtonIds.end(),
                        static_cast<int>(item->CtlID));
                    if (found != kButtonIds.end()) {
                        const auto label_index = static_cast<std::size_t>(
                            found - kButtonIds.begin());
                        draw_sidebar_action_button(*item,
                                                   kButtonLabels[label_index]);
                        return TRUE;
                    }
                }
                const auto* list_item =
                    reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
                std::optional<std::size_t> placeholder_source;
                if (list_item != nullptr && state->group_drag.has_value() &&
                    state->group_drag->dragging &&
                    state->group_drag->target_index.has_value() &&
                    *state->group_drag->target_index == list_item->itemID) {
                    placeholder_source = state->group_drag->source_index;
                }
                const bool dragged =
                    list_item != nullptr && state->group_drag.has_value() &&
                    state->group_drag->dragging &&
                    state->group_drag->source_index == list_item->itemID;
                if (state->sidebar.draw_item(
                        reinterpret_cast<DRAWITEMSTRUCT*>(lparam),
                        placeholder_source, dragged)) {
                    return TRUE;
                }
            }
            break;
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
                if (std::find(state->address_bars.begin(),
                              state->address_bars.end(),
                              control) != state->address_bars.end()) {
                    const HDC dc = reinterpret_cast<HDC>(wparam);
                    SetBkMode(dc, OPAQUE);
                    SetBkColor(dc, RGB(251, 252, 253));
                    SetTextColor(dc, RGB(76, 89, 107));
                    return reinterpret_cast<LRESULT>(
                        address_bar_background_brush());
                }
            }
            break;
        case WM_ERASEBKGND:
            if (state != nullptr) {
                paint_client_background(window,
                                        reinterpret_cast<HDC>(wparam), *state);
                return 1;
            }
            break;
        case WM_CONTEXTMENU: {
            if (state == nullptr) break;
            const HWND target = reinterpret_cast<HWND>(wparam);
            if (target != state->sidebar.window()) break;

            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (lparam == -1) {
                // Keyboard-invoked (Shift+F10 / menu key): anchor near the
                // currently selected row instead of a stale cursor position.
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
            if (HIWORD(hit) != 0 || index >= state->application.groups.size())
                return 0;  // Empty list or click landed outside any row.

            state->sidebar.set_selected_index(index);
            refresh_sidebar(*state);

            HMENU menu = CreatePopupMenu();
            if (menu == nullptr) return 0;
            AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kDuplicateGroupId),
                        L"Duplicate Group");
            AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kRenameGroupId),
                        L"Rename Group");
            AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(kDeleteGroupId),
                        L"Delete Group");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu,
                       MF_STRING | (index == 0 ? MF_GRAYED : 0),
                       static_cast<UINT_PTR>(kMoveUpId), L"Move Up");
            AppendMenuW(menu,
                       MF_STRING |
                           (index + 1 >= state->application.groups.size()
                                ? MF_GRAYED
                                : 0),
                       static_cast<UINT_PTR>(kMoveDownId), L"Move Down");
            SetForegroundWindow(window);
            const int command = TrackPopupMenu(
                menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0,
                window, nullptr);
            DestroyMenu(menu);
            if (command != 0)
                SendMessageW(window, WM_COMMAND, MAKEWPARAM(command, 0), 0);
            return 0;
        }
        case WM_COMMAND:
            if (state == nullptr) break;
            if (LOWORD(wparam) == kGroupListId && HIWORD(wparam) == LBN_SELCHANGE) {
                const auto selected = state->sidebar.selected_index();
                if (selected.has_value()) activate_group(window, *state, *selected);
                return 0;
            }
            if (HIWORD(wparam) == BN_CLICKED) {
                const int id = LOWORD(wparam);
                if (id >= kViewModeMenuIdBase &&
                    id < kViewModeMenuIdBase + kViewModeMenuIdCount) {
                    const int offset = id - kViewModeMenuIdBase;
                    const std::size_t pane_index = static_cast<std::size_t>(
                        offset / static_cast<int>(kViewModeOptions.size()));
                    const std::size_t mode_index = static_cast<std::size_t>(
                        offset % static_cast<int>(kViewModeOptions.size()));
                    set_pane_view_mode(*state, pane_index,
                                       kViewModeOptions[mode_index]);
                    return 0;
                }
                if (id >= kBackButtonIdBase &&
                    id < kBackButtonIdBase + static_cast<int>(kExplorerCount)) {
                    navigate_tab_history(*state,
                                         static_cast<std::size_t>(
                                             id - kBackButtonIdBase),
                                         true);
                    return 0;
                }
                if (id >= kForwardButtonIdBase &&
                    id < kForwardButtonIdBase +
                             static_cast<int>(kExplorerCount)) {
                    navigate_tab_history(*state,
                                         static_cast<std::size_t>(
                                             id - kForwardButtonIdBase),
                                         false);
                    return 0;
                }
                if (id >= kUpButtonIdBase &&
                    id < kUpButtonIdBase + static_cast<int>(kExplorerCount)) {
                    navigate_up(*state,
                                static_cast<std::size_t>(id - kUpButtonIdBase));
                    return 0;
                }
                if (id >= kRefreshButtonIdBase &&
                    id < kRefreshButtonIdBase + static_cast<int>(kExplorerCount)) {
                    refresh_pane(*state, static_cast<std::size_t>(
                                             id - kRefreshButtonIdBase));
                    return 0;
                }
                if (id >= kViewModeButtonIdBase &&
                    id < kViewModeButtonIdBase + static_cast<int>(kExplorerCount)) {
                    show_view_mode_menu(
                        window, *state,
                        static_cast<std::size_t>(id - kViewModeButtonIdBase));
                    return 0;
                }
                if (id >= kLayoutButtonIdBase &&
                    id < kLayoutButtonIdBase +
                             static_cast<int>(kLayoutButtonIds.size())) {
                    const std::size_t layout_index =
                        static_cast<std::size_t>(id - kLayoutButtonIdBase);
                    set_layout(window, *state, kLayoutTemplates[layout_index]);
                    return 0;
                }
                switch (LOWORD(wparam)) {
                    case kNewGroupId: add_group(window, *state); return 0;
                    case kDuplicateGroupId: duplicate_group(window, *state); return 0;
                    case kRenameGroupId: state->sidebar.begin_rename(); return 0;
                    case kDeleteGroupId: delete_group(window, *state); return 0;
                    case kMoveUpId: move_group(*state, false); return 0;
                    case kMoveDownId: move_group(*state, true); return 0;
                    default: break;
                }
            }
            break;
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
                        save_now(*state);
                    }
                }
            }
            return 0;
        case WM_SIZE:
            if (state != nullptr && FAILED(apply_layout(window, *state)))
                OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            release_navigation_icon_font();
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
            if (state != nullptr && has_active_group(*state)) {
                state->splitter_drag = splitter_at_point(
                    window, active_group(*state), point_from_lparam(lparam));
                if (state->splitter_drag.has_value()) {
                    SetCapture(window);
                    return 0;
                }
            }
            break;
        case WM_MOUSEMOVE:
            if (state != nullptr && state->splitter_drag.has_value() &&
                (wparam & MK_LBUTTON) != 0) {
                update_splitter_drag(window, *state,
                                     point_from_lparam(lparam));
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (state != nullptr && state->splitter_drag.has_value()) {
                update_splitter_drag(window, *state,
                                     point_from_lparam(lparam));
                state->splitter_drag.reset();
                save_now(*state);
                ReleaseCapture();
                return 0;
            }
            break;
        case WM_TIMER: {
            if (state == nullptr) break;
            const UINT_PTR timer = static_cast<UINT_PTR>(wparam);
            if (timer == kDragHoverSidebarTimerId &&
                state->sidebar_drag_target != nullptr) {
                state->sidebar_drag_target->timer_expired();
                return 0;
            }
            if (timer >= kDragHoverTabTimerIdBase &&
                timer < kDragHoverTabTimerIdBase + kExplorerCount) {
                const std::size_t pane_index = static_cast<std::size_t>(
                    timer - kDragHoverTabTimerIdBase);
                if (state->tab_drag_targets[pane_index] != nullptr)
                    state->tab_drag_targets[pane_index]->timer_expired();
                return 0;
            }
            break;
        }
        case WM_SETCURSOR:
            if (state != nullptr && has_active_group(*state) &&
                LOWORD(lparam) == HTCLIENT) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                const auto splitter =
                    splitter_at_point(window, active_group(*state), point);
                if (splitter.has_value()) {
                    SetCursor(LoadCursorW(
                        nullptr, splitter->vertical ? IDC_SIZEWE : IDC_SIZENS));
                    return TRUE;
                }
            }
            break;
        case WM_PARENTNOTIFY:
            if (state != nullptr && has_active_group(*state) &&
                LOWORD(wparam) == WM_MBUTTONDOWN) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                close_tab_at_point(window, *state, point);
                return 0;
            }
            if (state != nullptr && LOWORD(wparam) == WM_LBUTTONDOWN) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(window, &point);
                const std::size_t pane = pane_at_point(window, *state, point);
                if (pane < kExplorerCount) set_active_pane(window, *state, pane);
            }
            return 0;
        case WM_CLOSE:
            if (state != nullptr) {
                revoke_drag_hover_targets(*state);
                capture_window_placement(window, *state);
                save_now(*state, true);
                destroy_explorers(*state);
                assert(panedock::explorer_host::live_view_count() == 0);
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (state != nullptr) revoke_drag_hover_targets(*state);
            release_navigation_icon_font();
            release_address_bar_background_brush();
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            if (state != nullptr) {
                revoke_drag_hover_targets(*state);
                release_ui_font(*state);
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            }
            break;
        case WM_QUERYENDSESSION:
            if (state != nullptr) {
                capture_window_placement(window, *state);
                save_now(*state, true);
            }
            return TRUE;
        case WM_ENDSESSION:
            if (wparam) {
                if (state != nullptr) destroy_explorers(*state);
            }
            return 0;
        default: break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool register_window_class(HINSTANCE instance) noexcept {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    window_class.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClassName;
    return RegisterClassExW(&window_class) != 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
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
    if (FAILED(com_result)) return static_cast<int>(com_result);

    int exit_code = 1;
    if (!SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        OutputDebugStringW(L"PaneDock: SetProcessDpiAwarenessContext failed\n");
    if (!register_window_class(instance)) {
        OleUninitialize();
        return exit_code;
    }

    AppState state;
    const auto directory = session_directory();
    if (!directory.has_value()) {
        OutputDebugStringW(L"PaneDock: LocalAppData resolution failed\n");
        OleUninitialize();
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
    save_now(state);

    const auto& placement = state.application.window_placement;
    const wchar_t* const title = diagnostic_mode
                                     ? L"PaneDock \x2014 Diagnostic Mode"
                                     : L"PaneDock";
    HWND window = CreateWindowExW(
        0, kWindowClassName, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        placement.x,
        placement.y, placement.width, placement.height, nullptr, nullptr,
        instance, &state);
    if (window == nullptr) {
        destroy_explorers(state);
        assert(panedock::explorer_host::live_view_count() == 0);
        OleUninitialize();
        return exit_code;
    }
    ShowWindow(window, placement.maximized ? SW_SHOWMAXIMIZED : show_command);
    UpdateWindow(window);
    if (recovered_from_corruption &&
        session_source == panedock::core::SessionSource::backup) {
        MessageBoxW(
            window,
            L"PaneDock could not read its saved session and restored the "
            L"previous good version. Some recent changes may be missing.",
            L"PaneDock", MB_OK | MB_ICONWARNING);
    }
    if (recovered_from_corruption &&
        session_source == panedock::core::SessionSource::default_state) {
        MessageBoxW(
            window,
            L"PaneDock could not read its saved session or its backup and "
            L"started with a default Group. Your previous Groups could not "
            L"be recovered.",
            L"PaneDock", MB_OK | MB_ICONWARNING);
    }
    if (!clean_shutdown) {
        MessageBoxW(
            window,
            L"PaneDock did not shut down cleanly last time. If this keeps "
            L"happening, start it with --diagnostic to run without "
            L"third-party shell extensions.",
            L"PaneDock", MB_OK | MB_ICONWARNING);
    }
    MSG message{};
    int result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (has_active_group(state)) {
            const std::size_t active = active_pane_index(active_group(state));
            if (!address_bar_has_focus(state) &&
                state.explorers[active].translate_accelerator(&message) == S_OK)
                continue;
            const bool key_down = message.message == WM_KEYDOWN ||
                                  message.message == WM_SYSKEYDOWN;
            const bool control = GetKeyState(VK_CONTROL) < 0;
            const bool alt = GetKeyState(VK_MENU) < 0;
            const bool shift = GetKeyState(VK_SHIFT) < 0;
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
            if (message.message == WM_KEYDOWN && message.wParam == VK_F6) {
                const std::size_t count = active_group(state).panes.size();
                const std::size_t next = shift ? (active + count - 1) % count
                                               : (active + 1) % count;
                set_active_pane(window, state, next);
                continue;
            }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    exit_code = result < 0 ? 1 : static_cast<int>(message.wParam);

    destroy_explorers(state);
    assert(panedock::explorer_host::live_view_count() == 0);
    OleUninitialize();
    return exit_code;
}
