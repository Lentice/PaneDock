#include "unit/test_pane_host.h"
#include "unit/test_util.h"
#include "app_shell/window_helpers.h"
#include <filesystem>

namespace {

using panedock::test::TestPaneHost;

panedock::core::PaneState tab_state(int count) {
    panedock::core::PaneState state;
    for (int index = 0; index < count; ++index) {
        state.tabs.push_back({});
        state.tabs.back().id = "tab-" + std::to_string(index);
        state.tabs.back().location.parsing_name =
            L"Folder " + std::to_wstring(index);
    }
    state.active_tab_id = state.tabs.front().id;
    return state;
}

void test_refresh_bails_when_tab_list_changes_mid_scan() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    EXPECT(panedock::app_shell::Pane::register_window_class(instance));
    const HWND parent = CreateWindowExW(
        0, L"STATIC", nullptr, 0, 0, 0, 320, 200, nullptr, nullptr,
        instance, nullptr);
    EXPECT(parent != nullptr);
    TestPaneHost host;
    panedock::app_shell::Pane pane;
    EXPECT(pane.create(parent, 0));
    auto state = tab_state(3);
    pane.set_host(&host);
    pane.set_reentry_guard(&host.guard);
    pane.bind(&state);
    auto &strip = pane.tab_strip_ui();
    strip.refresh();
    EXPECT(strip.tab_visuals().size() == 3);
    wchar_t address[64]{};
    GetWindowTextW(pane.address_bar(), address, 64);
    EXPECT(std::wstring_view(address) == L"Folder 0");
    const auto previous = strip.tab_visuals();
    host.lookups = 0;
    host.mutate_on_second_lookup = &state;

    strip.refresh();

    EXPECT(host.lookups == 2);
    EXPECT(state.tabs.size() == 2);
    GetWindowTextW(pane.address_bar(), address, 64);
    EXPECT(std::wstring_view(address) == L"Folder 0");
    EXPECT(strip.tab_visuals().size() == previous.size());
    for (std::size_t index = 0; index < previous.size(); ++index)
        EXPECT(strip.tab_visuals()[index].text == previous[index].text);

    host.mutate_on_second_lookup = nullptr;
    state.active_tab_id = state.tabs.front().id;
    strip.refresh();
    EXPECT(strip.tab_visuals().size() == 2);
    EXPECT(strip.tab_visuals().front().text == L"Folder 1");
    GetWindowTextW(pane.address_bar(), address, 64);
    EXPECT(std::wstring_view(address) == L"Folder 1");
    pane.destroy();
    DestroyWindow(parent);
}

void test_scroll_offset_clamps_at_both_ends() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    EXPECT(panedock::app_shell::Pane::register_window_class(instance));
    const HWND parent = CreateWindowExW(
        0, L"STATIC", nullptr, 0, 0, 0, 320, 200, nullptr, nullptr,
        instance, nullptr);
    EXPECT(parent != nullptr);
    TestPaneHost host;
    panedock::app_shell::Pane pane;
    pane.set_host(&host);
    pane.set_reentry_guard(&host.guard);
    EXPECT(pane.create(parent, 0));
    auto state = tab_state(12);
    pane.bind(&state);
    auto &strip = pane.tab_strip_ui();
    EXPECT(SetWindowPos(strip.window(), nullptr, 0, 0, 280, 31,
                        SWP_NOZORDER | SWP_NOACTIVATE));
    strip.refresh();
    EXPECT(strip.tab_geometry().max_scroll_offset > 0);
    EXPECT(strip.tab_geometry().scroll_offset == 0);
    strip.scroll(false);
    EXPECT(strip.tab_geometry().scroll_offset == 0);
    for (int index = 0; index < 100; ++index) {
        strip.scroll(true);
        EXPECT(strip.tab_geometry().scroll_offset >= 0);
        EXPECT(strip.tab_geometry().scroll_offset <=
               strip.tab_geometry().max_scroll_offset);
    }
    EXPECT(strip.tab_geometry().scroll_offset ==
           strip.tab_geometry().max_scroll_offset);
    const int maximum = strip.tab_geometry().scroll_offset;
    strip.scroll(true);
    EXPECT(strip.tab_geometry().scroll_offset == maximum);
    for (int index = 0; index < 100; ++index) strip.scroll(false);
    EXPECT(strip.tab_geometry().scroll_offset == 0);

    // Exercise the local WNDPROC branches against the same geometry.
    strip.handle_message(WM_MOUSEWHEEL,
                         MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
    EXPECT(strip.tab_geometry().scroll_offset > 0);
    const auto left = strip.tab_geometry().scroll_button_rects[0];
    const POINT point{(left.left + left.right) / 2,
                      (left.top + left.bottom) / 2};
    strip.mouse_move(point);
    EXPECT(strip.scroll_hover_index() == 0);
    EXPECT(!strip.tab_hover_index().has_value());
    EXPECT(strip.handle_message(WM_LBUTTONDOWN, 0,
                                MAKELPARAM(point.x, point.y)) == 0);
    EXPECT(strip.tab_geometry().scroll_offset == 0);
    strip.handle_message(WM_MOUSELEAVE, 0, 0);
    EXPECT(!strip.scroll_hover_index().has_value());
    const auto first = strip.tab_geometry().tab_rects.front();
    strip.mouse_move({(first.left + first.right) / 2, 15});
    EXPECT(strip.tab_hover_index() == 0);
    const auto add = strip.tab_geometry().add_rect;
    strip.mouse_move({(add.left + add.right) / 2,
                      (add.top + add.bottom) / 2});
    EXPECT(strip.tab_hover_index() == state.tabs.size());

    const HWND strip_window = strip.window();
    pane.destroy();
    EXPECT(strip.window() == nullptr);
    EXPECT(!IsWindow(strip_window));
    EXPECT(strip.tab_visuals().empty());
    EXPECT(strip.tab_geometry().tab_rects.empty());
    DestroyWindow(parent);
}

void test_wider_tabs_and_full_name_tooltips() {
    // Match the shipped executable: v5 rejects sizeof(TOOLINFOW).
    const auto manifest = (std::filesystem::path(__FILE__).parent_path() / "../../resources/panedock.manifest").wstring();
    ACTCTXW context{};
    context.cbSize = sizeof(context);
    context.lpSource = manifest.c_str();
    const HANDLE activation = CreateActCtxW(&context);
    ULONG_PTR cookie{};
    EXPECT(activation != INVALID_HANDLE_VALUE && ActivateActCtx(activation, &cookie));
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    const auto instance = GetModuleHandleW(nullptr);
    EXPECT(panedock::app_shell::Pane::register_window_class(instance));
    const HWND parent = CreateWindowExW(0, L"STATIC", nullptr, 0, 0, 0, 1200, 200,
                                       nullptr, nullptr, instance, nullptr);
    struct TooltipHost final : TestPaneHost {
        HWND tooltip_window{};
        HWND tooltip() const noexcept override { return tooltip_window; }
    } host;
    host.tooltip_window = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, parent, nullptr, instance, nullptr);
    EXPECT(host.tooltip_window != nullptr);
    panedock::app_shell::Pane pane;
    pane.set_host(&host);
    pane.set_reentry_guard(&host.guard);
    EXPECT(pane.create(parent, 0));
    auto state = tab_state(3);
    state.tabs[0].location.parsing_name = L"addons";
    state.tabs[1].location.parsing_name = std::wstring(25, L'W');
    const std::wstring full_name = L"Timesheet & " + std::wstring(100, L'W');
    state.tabs[2].location.parsing_name = full_name;
    pane.bind(&state);
    auto& strip = pane.tab_strip_ui();
    SetWindowPos(strip.window(), nullptr, 0, 0, 1200, 31, SWP_NOZORDER | SWP_NOACTIVATE);
    strip.refresh();
    const auto& geometry = strip.tab_geometry();
    EXPECT(geometry.tab_rects[0].width() >= panedock::app_shell::scaled_value(strip.window(), 96));
    EXPECT(geometry.tab_rects[1].width() > panedock::app_shell::scaled_value(strip.window(), 200));
    const auto tooltip_at = [&](std::size_t index, std::wstring_view expected) {
        const auto rect = strip.tab_geometry().tab_rects[index];
        TTHITTESTINFOW hit{};
        hit.hwnd = strip.window();
        hit.pt = {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
        hit.ti.cbSize = sizeof(hit.ti);
        const bool found = SendMessageW(host.tooltip_window, TTM_HITTESTW, 0,
                                       reinterpret_cast<LPARAM>(&hit)) != 0;
        EXPECT(found == !expected.empty());
        if (found) {
            wchar_t text[1024]{};
            hit.ti.lpszText = text;
            SendMessageW(host.tooltip_window, TTM_GETTEXTW, 1024,
                         reinterpret_cast<LPARAM>(&hit.ti));
            EXPECT(std::wstring_view(text) == expected);
        }
    };
    tooltip_at(0, {}); // fully visible names need no tooltip
    tooltip_at(2, full_name);
    state.tabs[2].location.parsing_name = L"short";
    strip.refresh();
    tooltip_at(2, {}); // relabeling removes the old long name
    pane.destroy();
    EXPECT(SendMessageW(host.tooltip_window, TTM_GETTOOLCOUNT, 0, 0) == 3);
    DestroyWindow(host.tooltip_window);
    DestroyWindow(parent);
    DeactivateActCtx(0, cookie);
    ReleaseActCtx(activation);
}

} // namespace

int main() {
    test_refresh_bails_when_tab_list_changes_mid_scan();
    test_scroll_offset_clamps_at_both_ends();
    test_wider_tabs_and_full_name_tooltips();
    return panedock::test::summary("pane_tab_strip");
}
