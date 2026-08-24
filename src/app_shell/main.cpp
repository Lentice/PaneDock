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

namespace {

constexpr wchar_t kWindowClassName[] = L"PaneDockMainWindow";
constexpr int kLayoutToggleHotkeyId = 1;
constexpr std::size_t kExplorerCount = 4;
const std::array<std::wstring, kExplorerCount> kDefaultLocations{
    L"C:\\", L"C:\\Windows", L"C:\\Users", L"C:\\Program Files"};

struct AppState {
    std::array<panedock::explorer_host::ExplorerHost, kExplorerCount> explorers;
    std::array<bool, kExplorerCount> realized{};
    panedock::core::ApplicationState application;
    panedock::core::SessionDocument session_document;
    std::filesystem::path session_directory;
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

std::vector<panedock::core::PaneRect> layout_rects(
    HWND window, const panedock::core::GroupState& group) {
    const RECT client = client_rect(window);
    return panedock::core::compute_layout_rects(
        client.right - client.left, client.bottom - client.top,
        group.layout_template, group.divider_ratios);
}

void capture_locations(AppState& state) {
    auto& group = active_group(state);
    for (std::size_t index = 0; index < group.panes.size(); ++index) {
        if (state.realized[index] && !state.explorers[index].location().empty()) {
            active_tab(group.panes[index]).location.parsing_name =
                state.explorers[index].location();
        }
    }
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

void set_active_pane(AppState& state, std::size_t pane) noexcept {
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

void toggle_layout(HWND window, AppState& state) noexcept {
    auto& group = active_group(state);
    capture_locations(state);
    const std::size_t previous = active_pane_index(group);
    const auto target =
        group.layout_template == panedock::core::LayoutTemplate::four_pane_grid
            ? panedock::core::LayoutTemplate::left_right
            : panedock::core::LayoutTemplate::four_pane_grid;

    std::vector<std::string> pane_ids;
    std::vector<std::string> tab_ids;
    const std::size_t target_count = panedock::core::pane_count(target);
    for (std::size_t index = group.panes.size(); index < target_count; ++index) {
        pane_ids.push_back("pane-" + std::to_string(index));
        tab_ids.push_back("tab-" + std::to_string(index));
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

std::size_t pane_at_point(HWND window, const AppState& state,
                          POINT point) noexcept {
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
            if (FAILED(apply_layout(window, *state))) {
                MessageBoxW(window, L"PaneDock could not open the Shell view.",
                            L"PaneDock", MB_ICONERROR | MB_OK);
                destroy_explorers(*state);
                return -1;
            }
            const std::size_t active = active_pane_index(active_group(*state));
            state->explorers[active].set_active(true);
            state->explorers[active].focus();
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
            if (wparam == kLayoutToggleHotkeyId && state != nullptr)
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
        default:
            return DefWindowProcW(window, message, wparam, lparam);
    }
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
        const std::size_t active = active_pane_index(active_group(state));
        if (state.explorers[active].translate_accelerator(&message) == S_OK)
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    exit_code = result < 0 ? 1 : static_cast<int>(message.wParam);

    destroy_explorers(state);
    assert(panedock::explorer_host::live_view_count() == 0);
    OleUninitialize();
    return exit_code;
}
