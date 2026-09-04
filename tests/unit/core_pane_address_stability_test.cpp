#include "core/model.h"
#include "core/session.h"
#include "unit/test_util.h"

#include <string>
#include <utility>
#include <vector>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error "windows.h reached the core test seam"
#endif

namespace {
using namespace panedock::core;

const ShellLocation kDefault{L"default", L"known", L"fallback"};

TabState tab(std::string id) {
    return {std::move(id), kDefault, "details", "name", true, {}, 0};
}

GroupState group(std::string id = "group", LayoutTemplate layout =
                 LayoutTemplate::single) {
    GroupState value{id, L"Group", layout, default_divider_ratios(layout), {}, {}};
    reserve_panes(value);
    for (std::size_t index = 0; index < pane_count(layout); ++index) {
        const std::string pane_id = "pane-" + std::to_string(index);
        const std::string tab_id = "tab-" + std::to_string(index);
        value.panes.push_back({pane_id, {tab(tab_id)}, tab_id});
    }
    value.active_pane_id = value.panes.front().id;
    return value;
}

void test_add_group_keeps_pane_address() {
    ApplicationState application;
    EXPECT(add_group(application, group("observed")));
    PaneState* p = &application.groups.front().panes[0];
    for (std::size_t index = 1; index < 8; ++index) {
        EXPECT(add_group(application, group("group-" + std::to_string(index))));
    }
    EXPECT(application.groups.size() == 8);
    EXPECT(p == &application.groups.front().panes[0]);
    EXPECT(p->id == "pane-0");
}

void test_duplicate_group_keeps_pane_address() {
    ApplicationState application;
    EXPECT(add_group(application, group("observed")));
    PaneState* p = &application.groups.front().panes[0];
    EXPECT(duplicate_group(application, "observed", "copy", L"Copy"));
    EXPECT(p == &application.groups.front().panes[0]);
    EXPECT(p->id == "pane-0");
    EXPECT(application.groups.back().panes.capacity() == kMaxPaneCount);
}

void test_delete_group_keeps_later_pane_address() {
    ApplicationState application;
    EXPECT(add_group(application, group("deleted")));
    EXPECT(add_group(application, group("observed")));
    PaneState* p = &application.groups[1].panes[0];
    EXPECT(delete_group(application, "deleted"));
    EXPECT(p == &application.groups.front().panes[0]);
    EXPECT(p->id == "pane-0");
}

void test_reorder_group_keeps_pane_address() {
    ApplicationState application;
    EXPECT(add_group(application, group("first")));
    EXPECT(add_group(application, group("middle")));
    EXPECT(add_group(application, group("observed")));
    PaneState* p = &application.groups.back().panes[0];
    EXPECT(reorder_group(application, "observed", 0));
    EXPECT(p == &application.groups.front().panes[0]);
    EXPECT(p->id == "pane-0");
}

void test_switch_layout_keeps_pane_address() {
    GroupState value = group("observed");
    PaneState* p = &value.panes[0];
    EXPECT(switch_layout(value, LayoutTemplate::four_pane_grid, kDefault,
                         {"pane-1", "pane-2", "pane-3"},
                         {"tab-1", "tab-2", "tab-3"}));
    EXPECT(p == &value.panes[0]);
    EXPECT(p->id == "pane-0");
    EXPECT(value.panes.size() == 4);

    p = &value.panes[0];
    EXPECT(switch_layout(value, LayoutTemplate::single, kDefault));
    EXPECT(p == &value.panes[0]);
    EXPECT(p->id == "pane-0");
    EXPECT(value.panes.size() == 4);
}

void test_decode_reserves_panes() {
    ApplicationState application{1, {group("observed")}, "observed", {},
                                 kDefaultSidebarWidth, {}};
    auto restored = deserialize_session(
        serialize_session({application, {}, false}));
    EXPECT(restored.has_value());
    if (restored.has_value()) {
        EXPECT(restored->application.groups.front().panes.capacity() ==
               kMaxPaneCount);
        PaneState* p = &restored->application.groups.front().panes[0];
        EXPECT(p->id == "pane-0");
    }
}

}  // namespace

int main() {
    test_add_group_keeps_pane_address();
    test_duplicate_group_keeps_pane_address();
    test_delete_group_keeps_later_pane_address();
    test_reorder_group_keeps_pane_address();
    test_switch_layout_keeps_pane_address();
    test_decode_reserves_panes();
    return panedock::test::summary("core_pane_address_stability");
}
