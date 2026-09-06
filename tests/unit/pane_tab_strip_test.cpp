#include "app_shell/pane.h"
#include "app_shell/pane_host.h"
#include "app_shell/pane_message_dispatch.h"
#include "unit/test_util.h"

namespace panedock::app_shell {
std::optional<LRESULT> handle_pane_control_message(Pane&, UINT,
                                                  WPARAM, LPARAM) {
    return std::nullopt;
}
} // namespace panedock::app_shell

namespace {

class TestPaneHost final : public panedock::app_shell::PaneHost {
  public:
    panedock::core::PaneState *mutate_on_second_lookup{};
    int lookups{};

    bool is_shutting_down() const noexcept override { return false; }
    void shell_call_entered() noexcept override {}
    void shell_call_left() noexcept override {}
    void schedule_session_save() noexcept override {}
    const std::string &active_group_id() const noexcept override {
        static const std::string id{"group"};
        return id;
    }
    std::string make_unique_tab_id() const override { return "new"; }
    std::optional<panedock::app_shell::TabStripDragLayout> tab_drag_layout(
        const panedock::app_shell::Pane &, HWND, int, int, int) const override {
        return std::nullopt;
    }
    std::span<const panedock::app_shell::PinnedLocation>
    pinned_locations() const noexcept override { return {}; }
    void pin_location(panedock::core::ShellLocation) override {}
    bool location_capture_suppressed() const noexcept override { return false; }
    std::wstring tab_display_text(std::wstring_view name) override {
        if (++lookups == 2 && mutate_on_second_lookup != nullptr)
            mutate_on_second_lookup->tabs.erase(
                mutate_on_second_lookup->tabs.begin());
        return std::wstring(name);
    }
    HFONT chrome_font() const noexcept override {
        return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    HWND tooltip() const noexcept override { return nullptr; }
};

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

} // namespace

int main() {
    test_refresh_bails_when_tab_list_changes_mid_scan();
    test_scroll_offset_clamps_at_both_ends();
    return panedock::test::summary("pane_tab_strip");
}
