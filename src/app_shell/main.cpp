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
#include <cassert>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>

#include "app_shell/diagnostic_mode.h"
#include "core/layout.h"
#include "core/model.h"
#include "core/session.h"
#include "explorer_host/explorer_host.h"
#include "sidebar/sidebar.h"

namespace {

constexpr wchar_t kWindowClassName[] = L"PaneDockMainWindow";
constexpr int kLayoutToggleHotkeyId = 1;
constexpr std::size_t kExplorerCount = 4;
constexpr int kLayoutBarHeight = 44;
constexpr int kLayoutButtonHeight = 30;
constexpr int kLayoutButtonWidth = 30;
constexpr int kPaneCanvasPadding = 15;
constexpr int kPaneDividerThickness = 8;
constexpr int kSidebarHeadingHeight = 20;
constexpr int kTabStripHeight = 24;
constexpr int kTabStripIdBase = 200;
constexpr int kNavigationBarHeight = 28;
constexpr int kNavigationButtonWidth = 32;
constexpr int kBackButtonIdBase = 300;
constexpr int kForwardButtonIdBase = 310;
constexpr int kUpButtonIdBase = 320;
constexpr int kAddressBarIdBase = 330;
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
const std::array<std::wstring, kExplorerCount> kDefaultLocations{
    L"C:\\", L"C:\\Windows", L"C:\\Users", L"C:\\Program Files"};

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
    panedock::sidebar::Sidebar sidebar;
    std::array<HWND, kButtonIds.size()> sidebar_buttons{};
    HWND group_label{nullptr};
    HWND layout_label{nullptr};
    std::array<HWND, kLayoutButtonIds.size()> layout_buttons{};
    HWND empty_message{nullptr};
    std::array<HWND, kExplorerCount> tab_strips{};
    std::array<HWND, kExplorerCount> address_bars{};
    std::array<HWND, kExplorerCount> back_buttons{};
    std::array<HWND, kExplorerCount> forward_buttons{};
    std::array<HWND, kExplorerCount> up_buttons{};
    std::array<bool, kExplorerCount> suppress_history_record{};
};

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

void draw_layout_glyph(HDC dc, RECT rect, std::size_t index,
                       COLORREF color) noexcept {
    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    const int size = std::min(16, std::max(1, std::min(width, height) - 8));
    RECT glyph{rect.left + (rect.right - rect.left - size) / 2,
               rect.top + (rect.bottom - rect.top - size) / 2,
               rect.left + (rect.right - rect.left + size) / 2,
               rect.top + (rect.bottom - rect.top + size) / 2};
    HBRUSH frame_brush = CreateSolidBrush(color);
    if (frame_brush != nullptr) {
        FrameRect(dc, &glyph, frame_brush);
        DeleteObject(frame_brush);
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
                        std::size_t index) noexcept {
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool checked = SendMessageW(item.hwndItem, BM_GETCHECK, 0, 0) ==
                         BST_CHECKED;
    const COLORREF background = disabled
                                    ? RGB(245, 247, 249)
                                    : checked ? RGB(234, 241, 255)
                                              : RGB(248, 250, 252);
    const COLORREF glyph = disabled
                               ? RGB(148, 163, 184)
                               : checked ? RGB(37, 99, 235)
                                         : RGB(100, 116, 139);
    HBRUSH fill = CreateSolidBrush(background);
    if (fill != nullptr) {
        FillRect(item.hDC, &item.rcItem, fill);
        DeleteObject(fill);
    }
    if (checked) {
        HBRUSH border = CreateSolidBrush(RGB(207, 224, 255));
        if (border != nullptr) {
            FrameRect(item.hDC, &item.rcItem, border);
            DeleteObject(border);
        }
    }
    draw_layout_glyph(item.hDC, item.rcItem, index, glyph);
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

std::wstring tab_display_text(const panedock::core::TabState& tab) {
    const auto& parsing_name = tab.location.parsing_name;
    const std::size_t separator = parsing_name.find_last_of(L"\\/");
    if (separator == std::wstring::npos || separator + 1 == parsing_name.size())
        return parsing_name;
    return parsing_name.substr(separator + 1);
}

void refresh_tab_strip(AppState& state, std::size_t pane_index) {
    if (pane_index >= state.tab_strips.size()) return;
    const HWND strip = state.tab_strips[pane_index];
    SendMessageW(strip, TCM_DELETEALLITEMS, 0, 0);
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size()) {
        refresh_navigation_chrome(state, pane_index);
        return;
    }

    const auto& pane = active_group(state).panes[pane_index];
    std::size_t active_index = 0;
    for (std::size_t index = 0; index < pane.tabs.size(); ++index) {
        std::wstring text = tab_display_text(pane.tabs[index]);
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = text.data();
        SendMessageW(strip, TCM_INSERTITEMW, index,
                     reinterpret_cast<LPARAM>(&item));
        if (pane.tabs[index].id == pane.active_tab_id) active_index = index;
    }
    wchar_t plus[] = L"+";
    TCITEMW add_item{};
    add_item.mask = TCIF_TEXT;
    add_item.pszText = plus;
    SendMessageW(strip, TCM_INSERTITEMW, pane.tabs.size(),
                 reinterpret_cast<LPARAM>(&add_item));
    SendMessageW(strip, TCM_SETCURSEL, active_index, 0);
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
    const int gap = scaled_value(window, 4);
    const int header_height = std::min(
        scaled_value(window, kLayoutBarHeight),
        std::max(0, static_cast<int>(client.bottom - client.top)));
    const int label_width = scaled_value(window, 76);
    const int button_height = std::min(
        scaled_value(window, kLayoutButtonHeight), header_height);
    const int available_width = std::max(
        0, static_cast<int>(client.right) - sidebar_width - 2 * margin -
               label_width -
               static_cast<int>(kLayoutButtonIds.size() - 1) * gap);
    const int button_width = std::max(
        1, std::min(scaled_value(window, kLayoutButtonWidth),
                    available_width / static_cast<int>(kLayoutButtonIds.size())));
    int x = sidebar_width + margin;
    SetWindowPos(state.layout_label, nullptr, x, 0, label_width, header_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(state.layout_label, SW_SHOW);
    x += label_width + gap;
    for (HWND button : state.layout_buttons) {
        SetWindowPos(button, nullptr, x,
                     std::max(0, (header_height - button_height) / 2),
                     button_width, button_height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(button, SW_SHOW);
        x += button_width + gap;
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
    }
}

HFONT brand_font() noexcept {
    static const HFONT font = [] {
        LOGFONTW logfont{};
        HFONT base = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (base == nullptr || GetObjectW(base, sizeof(logfont), &logfont) == 0)
            return static_cast<HFONT>(nullptr);
        logfont.lfWeight = FW_BOLD;
        return CreateFontIndirectW(&logfont);
    }();
    return font;
}

void draw_brand_bar(HWND window, HDC dc, RECT rect) noexcept {
    HBRUSH background = CreateSolidBrush(RGB(251, 252, 254));
    if (background != nullptr) {
        FillRect(dc, &rect, background);
        DeleteObject(background);
    }
    RECT divider{rect.left, rect.bottom - scaled_value(window, 1), rect.right,
                rect.bottom};
    HBRUSH divider_brush = CreateSolidBrush(RGB(223, 229, 236));
    if (divider_brush != nullptr) {
        FillRect(dc, &divider, divider_brush);
        DeleteObject(divider_brush);
    }

    const int icon_size = scaled_value(window, 28);
    const int icon_margin = scaled_value(window, 14);
    const RECT icon{rect.left + icon_margin,
                    rect.top + ((rect.bottom - rect.top) - icon_size) / 2,
                    rect.left + icon_margin + icon_size,
                    rect.top + ((rect.bottom - rect.top) - icon_size) / 2 +
                        icon_size};
    HBRUSH icon_brush = CreateSolidBrush(RGB(37, 99, 235));
    HPEN icon_pen = CreatePen(PS_SOLID, 1, RGB(37, 99, 235));
    if (icon_brush != nullptr && icon_pen != nullptr) {
        const HGDIOBJ old_brush = SelectObject(dc, icon_brush);
        const HGDIOBJ old_pen = SelectObject(dc, icon_pen);
        const int radius = scaled_value(window, 6);
        RoundRect(dc, icon.left, icon.top, icon.right, icon.bottom, radius,
                  radius);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
    }
    if (icon_brush != nullptr) DeleteObject(icon_brush);
    if (icon_pen != nullptr) DeleteObject(icon_pen);

    const int glyph_inset = scaled_value(window, 6);
    HPEN glyph_pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    if (glyph_pen != nullptr) {
        const HGDIOBJ old_pen = SelectObject(dc, glyph_pen);
        const int mid_x = (icon.left + icon.right) / 2;
        const int mid_y = (icon.top + icon.bottom) / 2;
        MoveToEx(dc, mid_x, icon.top + glyph_inset, nullptr);
        LineTo(dc, mid_x, icon.bottom - glyph_inset);
        MoveToEx(dc, icon.left + glyph_inset, mid_y, nullptr);
        LineTo(dc, icon.right - glyph_inset, mid_y);
        SelectObject(dc, old_pen);
        DeleteObject(glyph_pen);
    }

    RECT title{icon.right + scaled_value(window, 10), rect.top,
              rect.right - scaled_value(window, 8), rect.bottom};
    const HFONT font = brand_font();
    const HGDIOBJ old_font = font != nullptr ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(30, 41, 59));
    DrawTextW(dc, L"PaneDock", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (old_font != nullptr) SelectObject(dc, old_font);
}

void paint_client_background(HWND window, HDC dc) noexcept {
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

    RECT divider{header.left, header.bottom - scaled_value(window, 1),
                 header.right, header.bottom};
    HBRUSH divider_brush = CreateSolidBrush(RGB(223, 229, 236));
    if (divider_brush != nullptr) {
        FillRect(dc, &divider, divider_brush);
        DeleteObject(divider_brush);
    }
}

void save_now(AppState& state, bool clean_shutdown = false) noexcept {
    capture_locations(state);
    state.session_document.application = state.application;
    state.session_document.clean_shutdown = clean_shutdown;
    if (!panedock::core::write_session(state.session_directory,
                                       state.session_document)) {
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
    if (!has_active_group(state)) {
        for (std::size_t index = 0; index < state.explorers.size(); ++index) {
            state.explorers[index].set_visible(false);
            ShowWindow(state.tab_strips[index], SW_HIDE);
            ShowWindow(state.back_buttons[index], SW_HIDE);
            ShowWindow(state.forward_buttons[index], SW_HIDE);
            ShowWindow(state.up_buttons[index], SW_HIDE);
            ShowWindow(state.address_bars[index], SW_HIDE);
        }
        ShowWindow(state.empty_message, SW_SHOW);
        write_live_view_count();
        return S_OK;
    }
    ShowWindow(state.empty_message, SW_HIDE);
    auto& group = active_group(state);
    const auto rects = layout_rects(window, group);
    for (std::size_t index = 0; index < state.explorers.size(); ++index) {
        const bool visible = index < group.panes.size();
        if (visible) {
            const RECT pane_rect = to_win32_rect(rects[index]);
            const int strip_height = scaled_value(window, kTabStripHeight);
            const int actual_strip_height =
                std::min(strip_height, static_cast<int>(pane_rect.bottom -
                                                        pane_rect.top));
            SetWindowPos(state.tab_strips[index], nullptr, pane_rect.left,
                         pane_rect.top, pane_rect.right - pane_rect.left,
                         actual_strip_height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(state.tab_strips[index], SW_SHOW);

            const int navigation_top = pane_rect.top + actual_strip_height;
            const int navigation_height = std::min(
                scaled_value(window, kNavigationBarHeight),
                std::max(0, static_cast<int>(pane_rect.bottom) -
                                navigation_top));
            const int pane_width = pane_rect.right - pane_rect.left;
            const int button_width = std::min(
                scaled_value(window, kNavigationButtonWidth), pane_width / 4);
            const std::array<HWND, 3> buttons{
                state.back_buttons[index], state.forward_buttons[index],
                state.up_buttons[index]};
            int x = pane_rect.left;
            for (HWND button : buttons) {
                SetWindowPos(button, nullptr, x, navigation_top, button_width,
                             navigation_height,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                ShowWindow(button, SW_SHOW);
                x += button_width;
            }
            SetWindowPos(state.address_bars[index], nullptr, x, navigation_top,
                         std::max(0, static_cast<int>(pane_rect.right) - x),
                         navigation_height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(state.address_bars[index], SW_SHOW);

            RECT rect = pane_rect;
            rect.top = navigation_top + navigation_height;
            if (!state.realized[index]) {
                const HRESULT hr = state.explorers[index].initialize(
                    window, rect,
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
                } catch (...) {
                    return E_OUTOFMEMORY;
                }
            } else {
                state.explorers[index].set_rect(rect);
            }
        } else {
            ShowWindow(state.tab_strips[index], SW_HIDE);
            ShowWindow(state.back_buttons[index], SW_HIDE);
            ShowWindow(state.forward_buttons[index], SW_HIDE);
            ShowWindow(state.up_buttons[index], SW_HIDE);
            ShowWindow(state.address_bars[index], SW_HIDE);
        }
        state.explorers[index].set_visible(visible);
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
    if (has_active_group(state)) {
        const std::size_t previous = active_pane_index(active_group(state));
        state.explorers[previous].set_active(false);
    }
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
    const std::size_t active = active_pane_index(group);
    state.explorers[active].set_active(true);
    state.explorers[active].focus();
    refresh_sidebar(state);
    save_now(state);
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
        const std::size_t active = active_pane_index(active_group(state));
        state.explorers[active].set_active(true);
        state.explorers[active].focus();
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
    if (deleted_active) {
        state.explorers[active_pane_index(active_group(state))].set_active(false);
    }
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
        const std::size_t active = active_pane_index(active_group(state));
        state.explorers[active].set_active(true);
        state.explorers[active].focus();
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

void set_active_pane(AppState& state, std::size_t pane) noexcept {
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    if (pane >= group.panes.size()) return;
    const std::size_t previous = active_pane_index(group);
    if (previous == pane ||
        !panedock::core::set_active_pane(group, group.panes[pane].id)) return;
    state.explorers[previous].set_active(false);
    state.explorers[pane].set_active(true);
    state.explorers[pane].focus();
    save_now(state);
}

panedock::core::LayoutTemplate next_layout(
    panedock::core::LayoutTemplate layout) noexcept {
    using panedock::core::LayoutTemplate;
    switch (layout) {
        case LayoutTemplate::single: return LayoutTemplate::left_right;
        case LayoutTemplate::left_right: return LayoutTemplate::top_bottom;
        case LayoutTemplate::top_bottom: return LayoutTemplate::three_pane;
        case LayoutTemplate::three_pane:
            return LayoutTemplate::four_pane_grid;
        case LayoutTemplate::four_pane_grid: return LayoutTemplate::single;
    }
    return LayoutTemplate::single;
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
    const std::size_t previous = active_pane_index(group);

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
    if (previous != active) {
        state.explorers[previous].set_active(false);
        state.explorers[active].set_active(true);
    }
    state.explorers[active].focus();
    save_now(state);
}

void toggle_layout(HWND window, AppState& state) noexcept {
    if (!has_active_group(state)) return;
    set_layout(window, state, next_layout(active_group(state).layout_template));
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
    TCHITTESTINFO hit{};
    hit.pt = point;
    MapWindowPoints(window, state.tab_strips[pane_index], &hit.pt, 1);
    const LRESULT item = SendMessageW(state.tab_strips[pane_index], TCM_HITTEST,
                                      0, reinterpret_cast<LPARAM>(&hit));
    const auto& tabs = active_group(state).panes[pane_index].tabs;
    if (item < 0 || static_cast<std::size_t>(item) >= tabs.size()) return;
    const std::string id = tabs[static_cast<std::size_t>(item)].id;
    close_tab_in_pane(window, state, pane_index, id);
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
            INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES};
            if (!InitCommonControlsEx(&controls)) return -1;
            if (!state->sidebar.create(window, kGroupListId)) return -1;
            state->group_label = CreateWindowExW(
                0, L"STATIC", L"GROUPS",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0,
                window, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (state->group_label == nullptr) return -1;
            SendMessageW(state->group_label, WM_SETFONT,
                         reinterpret_cast<WPARAM>(
                             GetStockObject(DEFAULT_GUI_FONT)),
                         TRUE);
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
                SendMessageW(state->sidebar_buttons[index], WM_SETFONT,
                             reinterpret_cast<WPARAM>(
                                 GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }
            state->layout_label = CreateWindowExW(
                0, L"STATIC", L"PANE LAYOUT",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0,
                window, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (state->layout_label == nullptr) return -1;
            SendMessageW(state->layout_label, WM_SETFONT,
                         reinterpret_cast<WPARAM>(
                             GetStockObject(DEFAULT_GUI_FONT)),
                         TRUE);
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
                SendMessageW(state->layout_buttons[index], WM_SETFONT,
                             reinterpret_cast<WPARAM>(
                                 GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }
            state->empty_message = CreateWindowExW(
                0, L"STATIC", L"No Group. Click New Group to get started.",
                WS_CHILD | SS_CENTER | SS_CENTERIMAGE, 0, 0, 0, 0, window,
                nullptr, GetModuleHandleW(nullptr), nullptr);
            if (state->empty_message == nullptr) return -1;
            SendMessageW(state->empty_message, WM_SETFONT,
                         reinterpret_cast<WPARAM>(
                             GetStockObject(DEFAULT_GUI_FONT)),
                         TRUE);
            for (std::size_t index = 0; index < state->tab_strips.size();
                 ++index) {
                state->tab_strips[index] = CreateWindowExW(
                    0, WC_TABCONTROLW, nullptr,
                    WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 0, 0,
                    window,
                    reinterpret_cast<HMENU>(kTabStripIdBase +
                                             static_cast<int>(index)),
                    GetModuleHandleW(nullptr), nullptr);
                if (state->tab_strips[index] == nullptr) return -1;
                SendMessageW(state->tab_strips[index], WM_SETFONT,
                             reinterpret_cast<WPARAM>(
                                 GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
                const std::array<const wchar_t*, 3> labels{L"<", L">", L"Up"};
                const std::array<int, 3> ids{
                    kBackButtonIdBase + static_cast<int>(index),
                    kForwardButtonIdBase + static_cast<int>(index),
                    kUpButtonIdBase + static_cast<int>(index)};
                const std::array<HWND*, 3> destinations{
                    &state->back_buttons[index],
                    &state->forward_buttons[index], &state->up_buttons[index]};
                for (std::size_t button = 0; button < labels.size(); ++button) {
                    *destinations[button] = CreateWindowExW(
                        0, L"BUTTON", labels[button],
                        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0,
                        window, reinterpret_cast<HMENU>(ids[button]),
                        GetModuleHandleW(nullptr), nullptr);
                    if (*destinations[button] == nullptr) return -1;
                    SendMessageW(*destinations[button], WM_SETFONT,
                                 reinterpret_cast<WPARAM>(
                                     GetStockObject(DEFAULT_GUI_FONT)),
                                 TRUE);
                }
                state->address_bars[index] = CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                    WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                    window,
                    reinterpret_cast<HMENU>(kAddressBarIdBase +
                                             static_cast<int>(index)),
                    GetModuleHandleW(nullptr), nullptr);
                if (state->address_bars[index] == nullptr ||
                    !SetWindowSubclass(state->address_bars[index],
                                       address_edit_proc, index,
                                       reinterpret_cast<DWORD_PTR>(state)))
                    return -1;
                SendMessageW(state->address_bars[index], WM_SETFONT,
                             reinterpret_cast<WPARAM>(
                                 GetStockObject(DEFAULT_GUI_FONT)),
                             TRUE);
            }
            refresh_sidebar(*state);
            refresh_tab_strips(*state);
            if (FAILED(apply_layout(window, *state))) {
                MessageBoxW(window, L"PaneDock could not open the Shell view.",
                            L"PaneDock", MB_ICONERROR | MB_OK);
                destroy_explorers(*state);
                return -1;
            }
            if (has_active_group(*state)) {
                const std::size_t active =
                    active_pane_index(active_group(*state));
                state->explorers[active].set_active(true);
                state->explorers[active].focus();
            }
            if (!RegisterHotKey(window, kLayoutToggleHotkeyId,
                                MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'L')) {
                MessageBoxW(
                    window, L"PaneDock could not register its layout hotkey.",
                    L"PaneDock", MB_ICONERROR | MB_OK);
                destroy_explorers(*state);
                return -1;
            }
            return 0;
        }
        case WM_NOTIFY:
            if (state != nullptr) {
                const auto* header = reinterpret_cast<const NMHDR*>(lparam);
                const auto pane_index =
                    tab_strip_index(*state, header->hwndFrom);
                if (pane_index.has_value() && header->code == TCN_SELCHANGE &&
                    has_active_group(*state) &&
                    *pane_index < active_group(*state).panes.size()) {
                    const LRESULT selected = SendMessageW(
                        state->tab_strips[*pane_index], TCM_GETCURSEL, 0, 0);
                    auto& pane = active_group(*state).panes[*pane_index];
                    if (selected == static_cast<LRESULT>(pane.tabs.size())) {
                        add_tab_to_pane(window, *state, *pane_index);
                    } else if (selected >= 0 &&
                               static_cast<std::size_t>(selected) <
                                   pane.tabs.size()) {
                        const std::string id =
                            pane.tabs[static_cast<std::size_t>(selected)].id;
                        switch_active_tab(window, *state, *pane_index, id);
                    }
                    return 0;
                }
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
                if (item != nullptr && item->CtlType == ODT_BUTTON &&
                    item->CtlID >= kLayoutButtonIdBase &&
                    item->CtlID < kLayoutButtonIdBase +
                                      static_cast<int>(kLayoutButtonIds.size())) {
                    draw_layout_button(
                        *item, static_cast<std::size_t>(item->CtlID -
                                                        kLayoutButtonIdBase));
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
                if (state->sidebar.draw_item(
                        reinterpret_cast<DRAWITEMSTRUCT*>(lparam)))
                    return TRUE;
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (state != nullptr) {
                const HWND control = reinterpret_cast<HWND>(lparam);
                if (control == state->group_label ||
                    control == state->layout_label) {
                    const HDC dc = reinterpret_cast<HDC>(wparam);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, RGB(152, 162, 179));
                    SetTextCharacterExtra(dc, scaled_value(window, 1));
                    return reinterpret_cast<LRESULT>(
                        GetSysColorBrush(COLOR_WINDOW));
                }
            }
            break;
        case WM_ERASEBKGND:
            if (state != nullptr) {
                paint_client_background(window,
                                        reinterpret_cast<HDC>(wparam));
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
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
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
                if (pane < kExplorerCount) set_active_pane(*state, pane);
            }
            return 0;
        case WM_HOTKEY:
            if (wparam == kLayoutToggleHotkeyId && state != nullptr &&
                has_active_group(*state))
                toggle_layout(window, *state);
            return 0;
        case WM_CLOSE:
            if (state != nullptr) {
                UnregisterHotKey(window, kLayoutToggleHotkeyId);
                capture_window_placement(window, *state);
                save_now(*state, true);
                destroy_explorers(*state);
                assert(panedock::explorer_host::live_view_count() == 0);
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            UnregisterHotKey(window, kLayoutToggleHotkeyId);
            PostQuitMessage(0);
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
            if (!diagnostic_mode) {
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
        0, kWindowClassName, title, WS_OVERLAPPEDWINDOW, placement.x,
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
                set_active_pane(state, next);
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
