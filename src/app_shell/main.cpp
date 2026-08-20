#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>

#include "explorer_host/explorer_host.h"
#include "app_shell/quadrant_layout.h"

namespace {

constexpr wchar_t kWindowClassName[] = L"PaneDockMainWindow";
constexpr int kLayoutToggleHotkeyId = 1;

struct AppState {
    std::array<panedock::explorer_host::ExplorerHost, 4> explorers;
    panedock::app_shell::LayoutState layout;
};

constexpr std::array<std::wstring_view, 4> kPaneLocations{
    L"C:\\", L"C:\\Windows", L"C:\\Users", L"C:\\Program Files"};

RECT client_rect(HWND window) noexcept {
    RECT rect{};
    GetClientRect(window, &rect);
    return rect;
}

void destroy_explorers(AppState& state) noexcept {
    for (auto& explorer : state.explorers) {
        explorer.destroy();
    }
}

HRESULT initialize_explorers(HWND window, AppState& state, const RECT& rect) {
    for (std::size_t index = 0; index < state.explorers.size(); ++index) {
        const HRESULT hr = state.explorers[index].initialize(
            window, rect, kPaneLocations[index]);
        if (FAILED(hr)) {
            destroy_explorers(state);
            return hr;
        }
    }
    return S_OK;
}

void apply_layout(HWND window, AppState& state) noexcept {
    const auto rects = panedock::app_shell::layout_rects(
        client_rect(window), state.layout.layout());
    for (std::size_t index = 0; index < state.explorers.size(); ++index) {
        if (state.layout.is_visible(index)) {
            state.explorers[index].set_rect(rects[index]);
        }
        state.explorers[index].set_visible(state.layout.is_visible(index));
    }
}

void set_active_pane(AppState& state, std::size_t pane) noexcept {
    const std::size_t previous = state.layout.active_pane();
    if (previous == pane || !state.layout.set_active_pane(pane)) {
        return;
    }

    state.explorers[previous].set_active(false);
    state.explorers[pane].set_active(true);
    state.explorers[pane].focus();
}

void toggle_layout(HWND window, AppState& state) noexcept {
    const std::size_t previous = state.layout.active_pane();
    state.layout.toggle_layout();
    apply_layout(window, state);

    const std::size_t active = state.layout.active_pane();
    if (previous != active) {
        state.explorers[previous].set_active(false);
        state.explorers[active].set_active(true);
    }
    state.explorers[active].focus();
}

std::size_t pane_at_point(HWND window, const AppState& state,
                          POINT point) noexcept {
    const auto rects = panedock::app_shell::layout_rects(
        client_rect(window), state.layout.layout());
    for (std::size_t index = 0; index < rects.size(); ++index) {
        if (state.layout.is_visible(index) && PtInRect(&rects[index], point)) {
            return index;
        }
    }
    return panedock::app_shell::LayoutState::kPaneCount;
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
        const RECT rect = client_rect(window);
        const HRESULT hr = initialize_explorers(window, *state, rect);
        if (FAILED(hr)) {
            MessageBoxW(window, L"PaneDock could not open the Shell view.",
                        L"PaneDock", MB_ICONERROR | MB_OK);
            return -1;
        }
        apply_layout(window, *state);
        state->explorers[state->layout.active_pane()].set_active(true);
        state->explorers[state->layout.active_pane()].focus();
        if (!RegisterHotKey(window, kLayoutToggleHotkeyId,
                            MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'L')) {
            MessageBoxW(window, L"PaneDock could not register its layout hotkey.",
                        L"PaneDock", MB_ICONERROR | MB_OK);
            destroy_explorers(*state);
            return -1;
        }
        return 0;
    }

    case WM_SIZE:
        if (state != nullptr) {
            apply_layout(window, *state);
        }
        return 0;

    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (state != nullptr) {
            apply_layout(window, *state);
        }
        return 0;
    }

    case WM_PARENTNOTIFY:
        if (state != nullptr && LOWORD(wparam) == WM_LBUTTONDOWN) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            const std::size_t pane = pane_at_point(window, *state, point);
            if (pane < panedock::app_shell::LayoutState::kPaneCount) {
                set_active_pane(*state, pane);
            }
        }
        return 0;

    case WM_HOTKEY:
        if (wparam == kLayoutToggleHotkeyId && state != nullptr) {
            toggle_layout(window, *state);
        }
        return 0;

    case WM_CLOSE:
        if (state != nullptr) {
            UnregisterHotKey(window, kLayoutToggleHotkeyId);
            // All panes are destroyed before their parent window. Their child
            // Shell view windows are torn down by IExplorerBrowser::Destroy.
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
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com_result)) {
        return static_cast<int>(com_result);
    }

    int exit_code = 1;
    if (!SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        OutputDebugStringW(L"PaneDock: SetProcessDpiAwarenessContext failed\n");
    }

    if (!register_window_class(instance)) {
        CoUninitialize();
        return exit_code;
    }

    AppState state;
    HWND window = CreateWindowExW(
        0, kWindowClassName, L"PaneDock", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700, nullptr, nullptr, instance,
        &state);
    if (window == nullptr) {
        destroy_explorers(state);
        assert(panedock::explorer_host::live_view_count() == 0);
        CoUninitialize();
        return exit_code;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    int result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result < 0) {
        exit_code = 1;
    } else {
        exit_code = static_cast<int>(message.wParam);
    }

    // WM_CLOSE already performed the ordered view teardown. This also covers
    // any other path that ends the message loop before WM_CLOSE is delivered.
    destroy_explorers(state);
    assert(panedock::explorer_host::live_view_count() == 0);
    CoUninitialize();
    return exit_code;
}
