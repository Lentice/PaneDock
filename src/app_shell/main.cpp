#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <shlobj.h>

#include "core/layout.h"
#include "core/model.h"
#include "core/session.h"
#include "explorer_host/explorer_host.h"
#include "sidebar/sidebar.h"

namespace {

constexpr wchar_t kWindowClassName[] = L"PaneDockMainWindow";
constexpr int kLayoutToggleHotkeyId = 1;
constexpr std::size_t kExplorerCount = 4;
constexpr int kGroupListId = 100;
constexpr int kNewGroupId = 101;
constexpr int kDuplicateGroupId = 102;
constexpr int kRenameGroupId = 103;
constexpr int kDeleteGroupId = 104;
constexpr int kMoveUpId = 105;
constexpr int kMoveDownId = 106;
constexpr std::array<int, 6> kButtonIds{kNewGroupId, kDuplicateGroupId,
                                        kRenameGroupId, kDeleteGroupId,
                                        kMoveUpId, kMoveDownId};
constexpr std::array<const wchar_t*, 6> kButtonLabels{
    L"New Group", L"Duplicate Group", L"Rename Group", L"Delete Group",
    L"Move Up", L"Move Down"};
const std::array<std::wstring, kExplorerCount> kDefaultLocations{
    L"C:\\", L"C:\\Windows", L"C:\\Users", L"C:\\Program Files"};

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
    HWND empty_message{nullptr};
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
                                 true}},
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

RECT pane_area(HWND window) noexcept {
    RECT area = client_rect(window);
    area.left = std::min(area.right,
                         area.left + scaled_value(
                                         window,
                                         panedock::sidebar::kSidebarWidth));
    return area;
}

LayoutMetrics layout_metrics(HWND window) noexcept {
    const double scale = static_cast<double>(GetDpiForWindow(window)) / 96.0;
    const auto scaled = [scale](int value) {
        return std::max(1, static_cast<int>(std::lround(value * scale)));
    };
    return {scaled(panedock::core::kMinimumPaneWidth),
            scaled(panedock::core::kMinimumPaneHeight),
            scaled(panedock::core::kDividerThickness)};
}

std::vector<panedock::core::PaneRect> layout_rects(
    HWND window, const panedock::core::GroupState& group) {
    const RECT client = pane_area(window);
    const LayoutMetrics metrics = layout_metrics(window);
    auto rects = panedock::core::compute_layout_rects(
        client.right - client.left, client.bottom - client.top,
        group.layout_template, group.divider_ratios,
        metrics.minimum_pane_width, metrics.minimum_pane_height,
        metrics.divider_thickness);
    for (auto& rect : rects) rect.x += client.left;
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

void capture_locations(AppState& state) {
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    for (std::size_t index = 0; index < group.panes.size(); ++index) {
        if (state.realized[index] && !state.explorers[index].location().empty()) {
            active_tab(group.panes[index]).location.parsing_name =
                state.explorers[index].location();
        }
    }
}

void refresh_sidebar(AppState& state) {
    std::vector<panedock::sidebar::GroupSummary> summaries;
    summaries.reserve(state.application.groups.size());
    std::optional<std::size_t> active_index;
    for (std::size_t index = 0; index < state.application.groups.size();
         ++index) {
        const auto& group = state.application.groups[index];
        summaries.push_back({group.id, group.name});
        if (group.id == state.application.active_group_id) active_index = index;
    }
    state.sidebar.set_groups(summaries);
    if (active_index.has_value()) state.sidebar.set_selected_index(*active_index);

    const auto selected = state.sidebar.selected_index();
    const bool has_selection = selected.has_value();
    EnableWindow(state.sidebar_buttons[0], TRUE);
    for (std::size_t index = 1; index < 4; ++index)
        EnableWindow(state.sidebar_buttons[index], has_selection);
    EnableWindow(state.sidebar_buttons[4],
                 has_selection && *selected > 0);
    EnableWindow(state.sidebar_buttons[5],
                 has_selection && *selected + 1 < state.application.groups.size());
}

void layout_sidebar(HWND window, AppState& state) noexcept {
    const RECT client = client_rect(window);
    const int width = std::min(static_cast<int>(client.right - client.left),
                               scaled_value(window,
                                            panedock::sidebar::kSidebarWidth));
    const int margin = scaled_value(window, 8);
    const int gap = scaled_value(window, 4);
    const int button_height = scaled_value(window, 28);
    const int controls_height = static_cast<int>(state.sidebar_buttons.size()) *
                                    button_height +
                                static_cast<int>(state.sidebar_buttons.size() - 1) * gap;
    RECT list_rect{margin, margin, std::max(margin, width - margin),
                   std::max(margin, static_cast<int>(client.bottom) - margin -
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

void save_now(AppState& state) noexcept {
    capture_locations(state);
    state.session_document.application = state.application;
    if (!panedock::core::write_session(state.session_directory,
                                       state.session_document)) {
        OutputDebugStringW(L"PaneDock: session persistence failed\n");
    }
}

void destroy_explorers(AppState& state) noexcept {
    for (auto& explorer : state.explorers) {
        explorer.destroy();
    }
    state.realized.fill(false);
}

HRESULT apply_layout(HWND window, AppState& state) {
    layout_sidebar(window, state);
    if (!has_active_group(state)) {
        for (auto& explorer : state.explorers) explorer.set_visible(false);
        ShowWindow(state.empty_message, SW_SHOW);
        return S_OK;
    }
    ShowWindow(state.empty_message, SW_HIDE);
    auto& group = active_group(state);
    const auto rects = layout_rects(window, group);
    for (std::size_t index = 0; index < state.explorers.size(); ++index) {
        const bool visible = index < group.panes.size();
        if (visible) {
            const RECT rect = to_win32_rect(rects[index]);
            if (!state.realized[index]) {
                const HRESULT hr = state.explorers[index].initialize(
                    window, rect,
                    active_tab(group.panes[index]).location.parsing_name);
                if (FAILED(hr)) {
                    return hr;
                }
                state.realized[index] = true;
            } else {
                state.explorers[index].set_rect(rect);
            }
        }
        state.explorers[index].set_visible(visible);
    }
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

void toggle_layout(HWND window, AppState& state) noexcept {
    if (!has_active_group(state)) return;
    auto& group = active_group(state);
    capture_locations(state);
    const std::size_t previous = active_pane_index(group);
    const auto target = next_layout(group.layout_template);

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

void update_splitter_drag(HWND window, AppState& state, POINT point) {
    if (!state.splitter_drag.has_value()) return;
    auto& group = active_group(state);
    const Splitter& drag = *state.splitter_drag;
    if (drag.ratio_index >= group.divider_ratios.size()) return;

    const RECT client = pane_area(window);
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
            if (!state->sidebar.create(window, kGroupListId)) return -1;
            for (std::size_t index = 0; index < state->sidebar_buttons.size();
                 ++index) {
                state->sidebar_buttons[index] = CreateWindowExW(
                    0, L"BUTTON", kButtonLabels[index],
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 0, 0, window,
                    reinterpret_cast<HMENU>(kButtonIds[index]),
                    GetModuleHandleW(nullptr), nullptr);
                if (state->sidebar_buttons[index] == nullptr) return -1;
                SendMessageW(state->sidebar_buttons[index], WM_SETFONT,
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
            refresh_sidebar(*state);
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
        case WM_MEASUREITEM:
            if (state != nullptr && state->sidebar.measure_item(
                                        reinterpret_cast<MEASUREITEMSTRUCT*>(lparam),
                                        GetDpiForWindow(window))) return TRUE;
            break;
        case WM_DRAWITEM:
            if (state != nullptr && state->sidebar.draw_item(
                                        reinterpret_cast<DRAWITEMSTRUCT*>(lparam)))
                return TRUE;
            break;
        case WM_COMMAND:
            if (state == nullptr) break;
            if (LOWORD(wparam) == kGroupListId && HIWORD(wparam) == LBN_SELCHANGE) {
                const auto selected = state->sidebar.selected_index();
                if (selected.has_value()) activate_group(window, *state, *selected);
                return 0;
            }
            if (HIWORD(wparam) == BN_CLICKED) {
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
                save_now(*state);
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
    if (loaded.recovered_from_corruption) {
        OutputDebugStringW(L"PaneDock: session recovery source=");
        OutputDebugStringW(session_source_name(loaded.source));
        OutputDebugStringW(
            L"\nTODO: surface session recovery in application chrome\n");
    }
    state.session_document = std::move(loaded.document);
    state.application = state.session_document.application;
    assert(panedock::core::is_valid(state.application));

    const auto& placement = state.application.window_placement;
    HWND window = CreateWindowExW(
        0, kWindowClassName, L"PaneDock", WS_OVERLAPPEDWINDOW, placement.x,
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
    MSG message{};
    int result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (has_active_group(state)) {
            const std::size_t active = active_pane_index(active_group(state));
            if (state.explorers[active].translate_accelerator(&message) == S_OK)
                continue;
            if (message.message == WM_KEYDOWN && message.wParam == VK_F6) {
                const std::size_t count = active_group(state).panes.size();
                const bool reverse = GetKeyState(VK_SHIFT) < 0;
                const std::size_t next = reverse ? (active + count - 1) % count
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
