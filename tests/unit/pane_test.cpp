#include "app_shell/pane.h"
#include "app_shell/pane_message_dispatch.h"
#include "app_shell/pane_host.h"
#include "unit/test_util.h"

// PD-189: pane_window_proc asks the coordinator (main.cpp) to handle its
// children's notifications. This test links Pane without the coordinator, so
// it stands in with a "not mine" answer; no test here pumps pane messages.
namespace panedock::app_shell {
std::optional<LRESULT> handle_pane_control_message(HWND, std::size_t, UINT,
                                                   WPARAM, LPARAM) {
    return std::nullopt;
}
}  // namespace panedock::app_shell

namespace {

class TestPaneHost final : public panedock::app_shell::PaneHost {
  public:
    bool shutting_down{};
    std::string group_id{"group-a"};
    bool suppress_location_capture{};

    bool is_shutting_down() const noexcept override { return shutting_down; }
    void shell_call_entered() noexcept override {}
    void shell_call_left() noexcept override {}
    void schedule_session_save() noexcept override {}
    const std::string &active_group_id() const noexcept override {
        return group_id;
    }
    std::string make_unique_tab_id() const override { return "tab-new"; }
    std::optional<panedock::app_shell::TabStripDragLayout> tab_drag_layout(
        const panedock::app_shell::Pane &, HWND, int, int,
        int) const override {
        return std::nullopt;
    }
    void tab_strip_needs_refresh(panedock::app_shell::Pane &) override {}
    bool location_capture_suppressed() const noexcept override {
        return suppress_location_capture;
    }
};

void test_pane_without_host_is_constructible() {
    panedock::app_shell::Pane pane;
    EXPECT(pane.pane_host() == nullptr);
}

void test_rect_cache_reports_only_real_changes() {
    panedock::app_shell::Pane pane;
    const RECT first{10, 20, 110, 220};
    const RECT same{10, 20, 110, 220};
    const RECT different{10, 20, 111, 220};

    EXPECT(pane.set_rect(first));
    EXPECT(!pane.set_rect(same));
    EXPECT(pane.set_rect(different));
    EXPECT(pane.laid_out_pane_rect().has_value());
    EXPECT(EqualRect(&pane.laid_out_pane_rect().value(), &different));
}

void test_set_tabs_replaces_visuals_without_owning_tab_state() {
    panedock::app_shell::Pane pane;
    const std::vector<std::wstring> labels{L"one", L"two", L"three"};
    pane.set_tabs(labels);
    EXPECT(pane.tab_visuals().size() == 3);
    EXPECT(pane.tab_visuals()[1].text == L"two");

    pane.set_tabs({});
    EXPECT(pane.tab_visuals().empty());
}

void test_tab_at_delegates_to_geometry_hit_test() {
    panedock::app_shell::Pane pane;
    panedock::app_shell::TabStripGeometry geometry;
    geometry.tab_rects.push_back({0, 0, 50, 20});
    geometry.viewport = {0, 0, 50, 20};
    pane.set_geometry(geometry);

    const auto hit = pane.tab_at(POINT{10, 10});
    EXPECT(hit.has_value() && *hit == 0);
    EXPECT(!pane.tab_at(POINT{100, 10}).has_value());
}

void test_pane_state_binding_uses_the_original_object() {
    panedock::app_shell::Pane pane;
    panedock::core::PaneState state{"pane", {}, "tab-1"};

    pane.bind(&state);
    EXPECT(pane.pane_state() == &state);
    pane.pane_state()->active_tab_id = "tab-2";
    EXPECT(state.active_tab_id == "tab-2");

    pane.unbind();
    EXPECT(pane.pane_state() == nullptr);
}

void test_destroy_unbinds_pane_state() {
    panedock::app_shell::Pane pane;
    panedock::core::PaneState state;
    pane.bind(&state);

    pane.destroy();

    EXPECT(pane.pane_state() == nullptr);
}

void test_controls_are_children_of_the_pane_window() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    EXPECT(panedock::app_shell::Pane::register_window_class(instance));
    const HWND parent =
        CreateWindowExW(0, L"STATIC", nullptr, 0, 0, 0, 100, 100, nullptr,
                        nullptr, instance, nullptr);
    EXPECT(parent != nullptr);

    panedock::app_shell::Pane pane;
    EXPECT(pane.create(parent, 0));
    EXPECT(GetParent(pane.window()) == parent);
    const HWND controls[]{pane.explorer_container(),
                          pane.tab_strip(),
                          pane.back_button(),
                          pane.forward_button(),
                          pane.up_button(),
                          pane.refresh_button(),
                          pane.view_mode_button(),
                          pane.pinned_button(),
                          pane.address_bar(),
                          pane.status_bar(),
                          pane.folder_context_button()};
    for (HWND control : controls)
        EXPECT(GetParent(control) == pane.window());

    pane.destroy();
    DestroyWindow(parent);
}

void test_active_tab_follows_the_bound_pane_state() {
    panedock::app_shell::Pane pane;
    EXPECT(pane.active_tab() == nullptr);

    panedock::core::PaneState state;
    state.tabs.push_back({});
    state.tabs.back().id = "tab-a";
    state.tabs.push_back({});
    state.tabs.back().id = "tab-b";
    state.active_tab_id = "tab-b";

    pane.bind(&state);
    EXPECT(pane.active_tab() == &state.tabs[1]);

    // A tab list that no longer contains the active id must report null
    // rather than hand out a stale element.
    state.tabs.erase(state.tabs.begin() + 1);
    EXPECT(pane.active_tab() == nullptr);

    pane.unbind();
    EXPECT(pane.active_tab() == nullptr);
}

void test_pending_navigation_is_per_pane() {
    panedock::app_shell::Pane first;
    panedock::app_shell::Pane second;
    first.pending_navigation().generation = 7;
    first.pending_navigation().tab_id = "tab-a";
    EXPECT(second.pending_navigation().generation == 0);
    EXPECT(second.pending_navigation().tab_id.empty());
    EXPECT(first.pending_navigation().generation == 7);
}

void test_navigation_request_identity_survives_group_switch() {
    panedock::app_shell::Pane pane;
    panedock::core::PaneState state;
    state.tabs.push_back({});
    state.tabs.front().id = "tab-a";
    state.active_tab_id = "tab-a";
    TestPaneHost host;

    pane.bind(&state);
    pane.set_host(&host);
    const auto generation = pane.begin_navigation();
    EXPECT(generation != 0);
    EXPECT(pane.navigation_request_is_current(generation));

    host.group_id = "group-b";
    EXPECT(!pane.navigation_request_is_current(generation));
    host.group_id = "group-a";
    EXPECT(pane.navigation_request_is_current(generation));
}

void test_navigation_calls_are_no_ops_without_host() {
    panedock::app_shell::Pane pane;
    panedock::core::ShellLocation location{};

    EXPECT(pane.begin_navigation() == 0);
    EXPECT(pane.navigate_to(location) == E_UNEXPECTED);
    EXPECT(pane.navigate_up_one_level() == E_UNEXPECTED);
    EXPECT(!pane.navigation_request_is_current(1));
    pane.record_navigation_result(location);
    pane.navigation_failed(1);
    pane.navigate_history(true);
    pane.navigate_up();
    pane.refresh_view();
}

void test_capture_location_respects_suppression() {
    panedock::app_shell::Pane pane;
    panedock::core::PaneState state;
    state.tabs.push_back({});
    state.tabs.front().id = "tab-a";
    state.active_tab_id = "tab-a";
    state.tabs.front().location.parsing_name = L"before";
    TestPaneHost host;
    host.suppress_location_capture = true;

    pane.bind(&state);
    pane.set_host(&host);
    pane.capture_location();

    EXPECT(state.tabs.front().location.parsing_name == L"before");
}

void test_add_tab_does_not_reuse_stale_tab_pointer() {
    panedock::app_shell::Pane pane;
    panedock::core::PaneState state;
    state.tabs.reserve(3);
    for (int index = 0; index < 3; ++index) {
        state.tabs.push_back({});
        state.tabs.back().id = "tab-" + std::to_string(index);
    }
    EXPECT(state.tabs.capacity() == 3);
    state.active_tab_id = "tab-0";
    TestPaneHost host;

    pane.bind(&state);
    pane.set_host(&host);
    pane.add_tab({{L"new-location"}, {}, {}});

    EXPECT(state.tabs.size() == 4);
    EXPECT(pane.active_tab() == &state.tabs.back());
}

void test_close_last_tab_leaves_pane_consistent() {
    panedock::app_shell::Pane pane;
    panedock::core::PaneState state;
    state.tabs.push_back({});
    state.tabs.front().id = "tab-only";
    state.tabs.front().location.parsing_name = L"old-location";
    state.active_tab_id = "tab-only";
    TestPaneHost host;

    pane.bind(&state);
    pane.set_host(&host);
    pane.close_tab("tab-only");

    EXPECT(state.tabs.size() == 1);
    EXPECT(state.active_tab_id == "tab-only");
    EXPECT(state.tabs.front().location.parsing_name ==
           L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
    EXPECT(pane.active_tab() == &state.tabs.front());
}

} // namespace

int main() {
    test_pane_without_host_is_constructible();
    test_rect_cache_reports_only_real_changes();
    test_set_tabs_replaces_visuals_without_owning_tab_state();
    test_tab_at_delegates_to_geometry_hit_test();
    test_pane_state_binding_uses_the_original_object();
    test_destroy_unbinds_pane_state();
    test_controls_are_children_of_the_pane_window();
    test_active_tab_follows_the_bound_pane_state();
    test_pending_navigation_is_per_pane();
    test_navigation_request_identity_survives_group_switch();
    test_navigation_calls_are_no_ops_without_host();
    test_capture_location_respects_suppression();
    test_add_tab_does_not_reuse_stale_tab_pointer();
    test_close_last_tab_leaves_pane_consistent();
    return panedock::test::summary("pane");
}
