#include "app_shell/pane.h"
#include "unit/test_util.h"

namespace {

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

} // namespace

int main() {
    test_rect_cache_reports_only_real_changes();
    test_set_tabs_replaces_visuals_without_owning_tab_state();
    test_tab_at_delegates_to_geometry_hit_test();
    test_pane_state_binding_uses_the_original_object();
    test_destroy_unbinds_pane_state();
    test_controls_are_children_of_the_pane_window();
    test_active_tab_follows_the_bound_pane_state();
    test_pending_navigation_is_per_pane();
    return panedock::test::summary("pane");
}
