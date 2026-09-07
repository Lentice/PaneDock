#include "unit/test_pane_host.h"
#include "unit/test_util.h"

namespace {

using panedock::test::TestPaneHost;

void test_replacement_navigation_does_not_inherit_history_suppression() {
    for (const bool shell_initiated : {false, true}) {
        TestPaneHost host;
        panedock::app_shell::Pane pane;
        pane.set_host(&host);
        const panedock::core::ShellLocation a{L"A", {}, {}};
        const panedock::core::ShellLocation b{L"B", {}, {}};
        const panedock::core::ShellLocation c{L"C", {}, {}};
        panedock::core::PaneState state{
            "pane", {{"tab", b, {}, {}, true, {a, b}, 1}}, "tab"};
        pane.bind(&state);

        // Back has changed the model, but its Shell completion is pending.
        EXPECT(panedock::core::navigate_tab_back(*pane.active_tab()));
        const auto back = pane.begin_navigation();
        pane.set_suppress_history(true);
        const auto replacement = shell_initiated ? back + 1
                                                : pane.begin_navigation();
        pane.navigation_complete(replacement, c);
        pane.navigation_complete(back, a); // late Back must stay ignored

        EXPECT(pane.active_tab()->history ==
               std::vector<panedock::core::ShellLocation>({a, c}));
        EXPECT(pane.active_tab()->history_index == 1);
        EXPECT(pane.active_tab()->location == c);
        EXPECT(!panedock::core::can_navigate_tab_forward(*pane.active_tab()));
        EXPECT(!pane.suppress_history());
        EXPECT(host.session_saves == 1);
    }
}

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
    pane.tab_strip_ui().set_tabs(labels);
    EXPECT(pane.tab_strip_ui().tab_visuals().size() == 3);
    EXPECT(pane.tab_strip_ui().tab_visuals()[1].text == L"two");

    pane.tab_strip_ui().set_tabs({});
    EXPECT(pane.tab_strip_ui().tab_visuals().empty());
}

void test_tab_at_delegates_to_geometry_hit_test() {
    panedock::app_shell::Pane pane;
    panedock::app_shell::TabStripGeometry geometry;
    geometry.tab_rects.push_back({0, 0, 50, 20});
    geometry.viewport = {0, 0, 50, 20};
    pane.tab_strip_ui().set_geometry(geometry);

    const auto hit = pane.tab_strip_ui().tab_at(POINT{10, 10});
    EXPECT(hit.has_value() && *hit == 0);
    EXPECT(!pane.tab_strip_ui().tab_at(POINT{100, 10}).has_value());
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
    pane.navigation_complete(1, location);
    pane.navigation_failed(1);
    pane.navigate_history(true);
    pane.navigate_up();
    pane.refresh_view();
}

void test_submit_address_is_a_no_op_while_shutting_down() {
    panedock::app_shell::Pane pane;
    panedock::core::PaneState state;
    state.tabs.push_back({});
    state.tabs.front().id = "tab-a";
    state.tabs.front().location.parsing_name = L"before";
    state.active_tab_id = "tab-a";
    TestPaneHost host;
    host.shutting_down = true;

    pane.bind(&state);
    pane.set_host(&host);
    pane.submit_address();

    EXPECT(state.tabs.front().location.parsing_name == L"before");
}

void test_navigation_completion_rejects_stale_results_and_refreshes_chrome() {
    TestPaneHost host;
    panedock::app_shell::Pane pane;
    panedock::core::PaneState state;
    state.tabs.push_back({});
    state.tabs.front().id = "tab-a";
    state.tabs.front().location.parsing_name = L"before";
    state.active_tab_id = "tab-a";
    pane.set_host(&host);
    pane.bind(&state);
    const auto generation = pane.begin_navigation();
    const panedock::core::ShellLocation completed{L"after", {}, {}};

    pane.navigation_complete(generation - 1, completed);
    host.group_id = "group-b";
    pane.navigation_complete(generation, completed);
    host.group_id = "group-a";
    host.shutting_down = true;
    pane.navigation_complete(generation, completed);
    EXPECT(pane.active_tab()->location.parsing_name == L"before");
    EXPECT(host.session_saves == 0);

    host.shutting_down = false;
    pane.navigation_complete(generation, completed);
    EXPECT(pane.active_tab()->location == completed);
    EXPECT(pane.active_tab()->history.back() == completed);
    EXPECT(pane.tab_strip_ui().tab_visuals().front().text == L"after");
    EXPECT(host.session_saves == 1);

    pane.set_suppress_history(true);
    const auto history_size = pane.active_tab()->history.size();
    pane.navigation_complete(generation, {L"restored", {}, {}});
    EXPECT(!pane.suppress_history());
    EXPECT(pane.active_tab()->history.size() == history_size);
    EXPECT(pane.active_tab()->history.back().parsing_name == L"restored");
    EXPECT(pane.tab_strip_ui().tab_visuals().front().text == L"restored");
    EXPECT(host.session_saves == 2);

    // Coordinator-only and unknown IDs must remain available to the caller.
    EXPECT(!pane.handle_command(panedock::app_shell::encode_pane_control(
        panedock::app_shell::PaneControl::folder_context)));
    EXPECT(!pane.handle_command(-1));
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

void test_tab_commands_schedule_only_successful_changes() {
    TestPaneHost host;
    panedock::core::PaneState state{
        "pane", {{"a", {L"A", {}, {}}, {}, {}, true, {}, 0},
                 {"b", {L"B", {}, {}}, {}, {}, true, {}, 0}}, "a"};
    panedock::app_shell::Pane pane;
    pane.set_host(&host);
    pane.bind(&state);

    pane.switch_active_tab("a");
    pane.switch_active_tab("missing");
    pane.close_tab("missing");
    EXPECT(host.session_saves == 0);
    EXPECT(state.active_tab_id == "a");

    pane.switch_active_tab("b");
    EXPECT(state.active_tab_id == "b");
    EXPECT(host.session_saves == 1);
    pane.close_tab("a"); // Closing an inactive tab still saves the model.
    EXPECT(state.active_tab_id == "b");
    EXPECT(pane.active_tab()->location.parsing_name == L"B");
    EXPECT(host.session_saves == 2);
    pane.add_tab({L"new", {}, {}});
    EXPECT(state.active_tab_id == "tab-new");
    EXPECT(host.session_saves == 3);
    pane.add_tab({L"duplicate", {}, {}}); // Host returns the same ID.
    EXPECT(host.session_saves == 3);
    pane.close_tab("tab-new");
    EXPECT(state.active_tab_id == "b");
    EXPECT(host.session_saves == 4);
    EXPECT(!pane.realized());

    const auto before_shutdown = state;
    host.shutting_down = true;
    pane.switch_active_tab("b");
    pane.add_tab({L"blocked", {}, {}});
    pane.close_tab("b");
    EXPECT(state == before_shutdown);
    EXPECT(host.session_saves == 4);
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

void test_close_tabs_preserves_the_requested_set_and_last_tab_fallback() {
    using namespace panedock::app_shell;
    for (const int command :
         {kCloseOtherTabsId, kCloseAllTabsId, kCloseTabsToRightId}) {
        TestPaneHost host;
        panedock::core::PaneState state;
        for (const auto *id : {"a", "b", "c", "d"}) {
            state.tabs.push_back({id, {L"folder", {}, {}}, "details",
                                  "System.ItemNameDisplay", false,
                                  {{L"previous", {}, {}}}, 0});
        }
        state.active_tab_id = "c";
        Pane pane;
        pane.set_host(&host);
        pane.bind(&state);
        const auto original = state;
        host.shutting_down = true;
        pane.close_tabs("b", command);
        EXPECT(state == original);
        host.shutting_down = false;
        pane.close_tabs("b", -1);
        EXPECT(state == original);

        pane.close_tabs("b", command);

        std::vector<std::string> remaining;
        for (const auto &tab : state.tabs) remaining.push_back(tab.id);
        if (command == kCloseOtherTabsId) {
            EXPECT(remaining == std::vector<std::string>{"b"});
            EXPECT(state.active_tab_id == "b");
            EXPECT(state.tabs.front() == original.tabs[1]);
            EXPECT(host.session_saves == 3);
        } else if (command == kCloseTabsToRightId) {
            const std::vector<std::string> expected{"a", "b"};
            EXPECT(remaining == expected);
            EXPECT(state.active_tab_id == "b");
            EXPECT(state.tabs[0] == original.tabs[0]);
            EXPECT(state.tabs[1] == original.tabs[1]);
            EXPECT(host.session_saves == 2);
            const auto after = state;
            pane.close_tabs("missing", command);
            pane.close_tabs("b", command);
            EXPECT(state == after);
            EXPECT(host.session_saves == 2);
        } else {
            EXPECT(remaining == std::vector<std::string>{"d"});
            EXPECT(state.active_tab_id == "d");
            EXPECT(state.tabs.front().location.parsing_name ==
                   L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
            EXPECT(state.tabs.front().history.empty());
            EXPECT(state.tabs.front().history_index == 0);
            EXPECT(state.tabs.front().view_mode == "details");
            EXPECT(state.tabs.front().sort_column == "System.ItemNameDisplay");
            EXPECT(!state.tabs.front().sort_ascending);
            EXPECT(host.session_saves == 4);
            pane.close_tabs("d", command);
            EXPECT(state.tabs.size() == 1);
            EXPECT(state.active_tab_id == "d");
            EXPECT(host.session_saves == 5);
        }
        EXPECT(pane.active_tab() != nullptr);
    }
}

} // namespace

int main() {
    test_tab_commands_schedule_only_successful_changes();
    test_replacement_navigation_does_not_inherit_history_suppression();
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
    test_submit_address_is_a_no_op_while_shutting_down();
    test_navigation_completion_rejects_stale_results_and_refreshes_chrome();
    test_capture_location_respects_suppression();
    test_add_tab_does_not_reuse_stale_tab_pointer();
    test_close_last_tab_leaves_pane_consistent();
    test_close_tabs_preserves_the_requested_set_and_last_tab_fallback();
    return panedock::test::summary("pane");
}
