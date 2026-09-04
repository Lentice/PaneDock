#include "app_shell/pane.h"
#include "unit/test_util.h"

namespace {

void test_rect_cache_reports_only_real_changes() {
    panedock::app_shell::Pane pane;
    const RECT first{10, 20, 110, 220};
    const RECT same{10, 20, 110, 220};
    const RECT different{10, 20, 111, 220};

    EXPECT(pane.set_rect(first, nullptr));
    EXPECT(!pane.set_rect(same, nullptr));
    EXPECT(pane.set_rect(different, nullptr));
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

}  // namespace

int main() {
    test_rect_cache_reports_only_real_changes();
    test_set_tabs_replaces_visuals_without_owning_tab_state();
    test_tab_at_delegates_to_geometry_hit_test();
    return panedock::test::summary("pane");
}
