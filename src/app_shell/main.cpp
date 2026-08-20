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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include <shlobj.h>

#include "core/prototype_location_persistence.h"
#include "explorer_host/explorer_host.h"
#include "app_shell/quadrant_layout.h"

namespace {

constexpr wchar_t kWindowClassName[] = L"PaneDockMainWindow";
constexpr int kLayoutToggleHotkeyId = 1;
constexpr wchar_t kPersistenceFileName[] = L"prototype-state.txt";
constexpr wchar_t kPersistenceBackupName[] = L"prototype-state.txt.bak";

const std::array<std::wstring, 4> kDefaultLocations{
    L"C:\\", L"C:\\Windows", L"C:\\Users", L"C:\\Program Files"};

struct PersistenceFiles final {
    std::filesystem::path directory;
    std::filesystem::path primary;
    std::filesystem::path backup;
};

struct AppState {
    std::array<panedock::explorer_host::ExplorerHost, 4> explorers;
    panedock::app_shell::LayoutState layout;
    std::array<std::wstring, 4> locations{kDefaultLocations};
    std::optional<PersistenceFiles> persistence;
};

std::optional<PersistenceFiles> persistence_files() noexcept {
    PWSTR local_app_data = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                    &local_app_data))) {
        return std::nullopt;
    }

    PersistenceFiles files;
    files.directory = std::filesystem::path(local_app_data) / L"PaneDock";
    CoTaskMemFree(local_app_data);
    files.primary = files.directory / kPersistenceFileName;
    files.backup = files.directory / kPersistenceBackupName;
    return files;
}

std::optional<std::wstring> read_utf8_file(
    const std::filesystem::path& path) noexcept {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    const std::string bytes((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    if (input.bad()) {
        return std::nullopt;
    }

    if (bytes.empty()) {
        return std::wstring{};
    }
    const int character_count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (character_count <= 0) {
        return std::nullopt;
    }
    std::wstring text(static_cast<std::size_t>(character_count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                            static_cast<int>(bytes.size()), text.data(),
                            character_count) != character_count) {
        return std::nullopt;
    }
    return text;
}

std::optional<std::string> utf8_text(std::wstring_view text) noexcept {
    if (text.empty()) {
        return std::string{};
    }
    const int byte_count = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (byte_count <= 0) {
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(byte_count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), bytes.data(),
                            byte_count, nullptr, nullptr) != byte_count) {
        return std::nullopt;
    }
    return bytes;
}

panedock::core::PrototypeLocationState load_location_state(
    const std::optional<PersistenceFiles>& files) noexcept {
    if (!files.has_value()) {
        return {kDefaultLocations};
    }

    const auto contents = read_utf8_file(files->primary);
    if (!contents.has_value()) {
        return {kDefaultLocations};
    }

    try {
        return panedock::core::parse_prototype_location_state(
            *contents, kDefaultLocations);
    } catch (...) {
        return {kDefaultLocations};
    }
}

bool write_location_state(const PersistenceFiles& files,
                          const panedock::core::PrototypeLocationState& state)
    noexcept {
    const auto bytes = utf8_text(
        panedock::core::serialize_prototype_location_state(state));
    if (!bytes.has_value()) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(files.directory, error);
    if (error) {
        return false;
    }

    const std::filesystem::path temporary = files.primary.wstring() + L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
        output.flush();
        if (!output) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    const bool primary_exists =
        std::filesystem::exists(files.primary, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    if (primary_exists &&
        !CopyFileW(files.primary.c_str(), files.backup.c_str(), FALSE)) {
        std::filesystem::remove(temporary, error);
        return false;
    }

    if (!MoveFileExW(temporary.c_str(), files.primary.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

panedock::core::PrototypeLocationState capture_location_state(
    const AppState& state) {
    panedock::core::PrototypeLocationState saved;
    saved.locations = state.locations;
    for (std::size_t pane = 0; pane < saved.locations.size(); ++pane) {
        if (!state.explorers[pane].location().empty()) {
            saved.locations[pane] = state.explorers[pane].location();
        }
    }
    saved.layout = state.layout.layout() ==
                           panedock::app_shell::LayoutTemplate::two_pane
                       ? panedock::core::PrototypeLayout::two_pane
                       : panedock::core::PrototypeLayout::four_pane;
    saved.active_pane = state.layout.active_pane();
    return saved;
}

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
            window, rect, state.locations[index]);
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
            if (state->persistence.has_value() &&
                !write_location_state(*state->persistence,
                                      capture_location_state(*state))) {
                OutputDebugStringW(L"PaneDock: location persistence failed\n");
            }
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
    state.persistence = persistence_files();
    const auto loaded = load_location_state(state.persistence);
    state.locations = loaded.locations;
    state.layout.restore(
        loaded.layout == panedock::core::PrototypeLayout::two_pane
            ? panedock::app_shell::LayoutTemplate::two_pane
            : panedock::app_shell::LayoutTemplate::four_pane,
        loaded.active_pane);
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
