#include "core/model.h"
#include "unit/test_util.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error "windows.h reached the core test seam"
#endif

namespace {
using namespace panedock::core;

const ShellLocation kDefault{L"default", L"known", L"fallback"};

TabState tab(std::string id, std::wstring location = L"location") {
    return {std::move(id), {std::move(location), L"", L"fallback"},
            "details", "name", true};
}

PaneState pane(std::string id, std::vector<TabState> tabs) {
    const std::string active = tabs.front().id;
    return {std::move(id), std::move(tabs), active};
}

GroupState group(std::string id = "group-1",
                 LayoutTemplate layout = LayoutTemplate::single) {
    GroupState value{id, L"Group", layout, default_divider_ratios(layout), {}, {}};
    for (std::size_t index = 0; index < pane_count(layout); ++index) {
        value.panes.push_back(pane("pane-" + std::to_string(index + 1),
                                   {tab("tab-" + std::to_string(index + 1))}));
    }
    value.active_pane_id = value.panes.front().id;
    return value;
}

void test_layout_metadata() {
    struct Case { LayoutTemplate layout; std::size_t panes; std::size_t ratios; };
    const Case cases[]{{LayoutTemplate::single, 1, 0},
                       {LayoutTemplate::left_right, 2, 1},
                       {LayoutTemplate::top_bottom, 2, 1},
                       {LayoutTemplate::three_pane, 3, 2},
                       {LayoutTemplate::two_over_one, 3, 2},
                       {LayoutTemplate::one_over_two, 3, 2},
                       {LayoutTemplate::two_beside_one, 3, 2},
                       {LayoutTemplate::four_pane_grid, 4, 2}};
    for (const auto& item : cases) {
        EXPECT(pane_count(item.layout) == item.panes);
        EXPECT(divider_ratio_count(item.layout) == item.ratios);
        EXPECT(default_divider_ratios(item.layout).size() == item.ratios);
    }
}

void test_invariants_reject_deliberate_breakage() {
    auto expect_invalid = [](auto break_group) {
        GroupState value = group("group", LayoutTemplate::left_right);
        break_group(value);
        EXPECT(!is_valid(value));
    };

    expect_invalid([](GroupState& value) { value.panes.pop_back(); });
    expect_invalid([](GroupState& value) { value.active_pane_id = "absent"; });
    expect_invalid([](GroupState& value) {
        value.panes.front().active_tab_id = "absent";
    });
    expect_invalid([](GroupState& value) { value.panes.front().tabs.clear(); });
    expect_invalid([](GroupState& value) { value.divider_ratios.clear(); });
    expect_invalid([](GroupState& value) { value.divider_ratios.front() = -0.1; });
    expect_invalid([](GroupState& value) { value.divider_ratios.front() = 1.1; });
    expect_invalid([](GroupState& value) {
        value.divider_ratios.front() = std::numeric_limits<double>::quiet_NaN();
    });
    expect_invalid([](GroupState& value) {
        value.panes.back().id = value.panes.front().id;
    });
    expect_invalid([](GroupState& value) {
        value.panes.front().tabs.push_back(value.panes.front().tabs.front());
    });
    expect_invalid([](GroupState& value) {
        auto& item = value.panes.front().tabs.front();
        item.history = {item.location};
        item.history_index = 1;
    });
    expect_invalid([](GroupState& value) {
        auto& item = value.panes.front().tabs.front();
        item.history = {ShellLocation{L"other", {}, {}}};
    });

    ApplicationState application{1, {group("same"), group("same")}, "same",
                                 {}, kDefaultSidebarWidth, {}};
    EXPECT(!is_valid(application));
}

void test_tab_navigation_history() {
    TabState value = tab("tab", L"one");
    EXPECT(is_valid(group()));
    EXPECT(!can_navigate_tab_back(value));
    EXPECT(!can_navigate_tab_forward(value));
    EXPECT(!navigate_tab_back(value));
    EXPECT(!navigate_tab_forward(value));

    record_navigation(value, {L"two", {}, L"fallback"});
    record_navigation(value, {L"three", {}, L"fallback"});
    record_navigation(value, {L"four", {}, L"fallback"});
    EXPECT(value.history.size() == 4);
    EXPECT(navigate_tab_back(value));
    EXPECT(value.location.parsing_name == L"three");
    EXPECT(navigate_tab_back(value));
    EXPECT(value.location.parsing_name == L"two");
    EXPECT(navigate_tab_forward(value));
    EXPECT(value.location.parsing_name == L"three");
    EXPECT(navigate_tab_forward(value));
    EXPECT(value.location.parsing_name == L"four");
    EXPECT(!navigate_tab_forward(value));

    EXPECT(navigate_tab_back(value));
    record_navigation(value, {L"branch", {}, L"fallback"});
    EXPECT(value.history.size() == 4);
    EXPECT(value.history.back().parsing_name == L"branch");
    EXPECT(!can_navigate_tab_forward(value));

    const auto unchanged = value;
    record_navigation(value, value.location);
    EXPECT(value == unchanged);

    GroupState valid = group();
    valid.panes.front().tabs.front() = value;
    valid.panes.front().active_tab_id = value.id;
    EXPECT(is_valid(valid));
}

void test_group_mutations() {
    ApplicationState application;
    EXPECT(add_group(application, group("one")));
    EXPECT(application.active_group_id == "one");
    EXPECT(add_group(application, group("two")));
    EXPECT(rename_group(application, "one", L"Renamed"));
    EXPECT(application.groups.front().name == L"Renamed");

    const GroupState original = application.groups.front();
    EXPECT(duplicate_group(application, "one", "copy", L"Copy"));
    EXPECT(application.groups.back().id == "copy");
    EXPECT(application.groups.back().name == L"Copy");
    GroupState expected = original;
    expected.id = "copy";
    expected.name = L"Copy";
    EXPECT(application.groups.back() == expected);

    EXPECT(reorder_group(application, "copy", 0));
    EXPECT(application.groups.front().id == "copy");
    EXPECT(delete_group(application, "one"));
    EXPECT(is_valid(application));
    EXPECT(delete_group(application, "two"));
    EXPECT(application.active_group_id == "copy");
    EXPECT(delete_group(application, "copy"));
    EXPECT(application.groups.empty());
    EXPECT(application.active_group_id.empty());
    EXPECT(is_valid(application));
}

void test_reorder_projection() {
    const auto expect_projection = [](std::size_t source, std::size_t target,
                                      std::initializer_list<std::size_t> expected) {
        std::size_t destination = 0;
        for (const std::size_t source_index : expected) {
            EXPECT(reorder_source_index(expected.size(), source, target,
                                        destination++) == source_index);
        }
    };

    expect_projection(0, 3, {1, 2, 3, 0});
    expect_projection(3, 0, {3, 0, 1, 2});
    expect_projection(1, 3, {0, 2, 3, 1});
    expect_projection(2, 0, {2, 0, 1, 3});
    expect_projection(1, 1, {0, 1, 2, 3});
    expect_projection(0, 0, {0});

    for (std::size_t count = 1; count <= 6; ++count) {
        for (std::size_t source = 0; source < count; ++source) {
            for (std::size_t target = 0; target < count; ++target) {
                std::vector<std::size_t> expected;
                for (std::size_t index = 0; index < count; ++index) {
                    if (index != source) expected.push_back(index);
                }
                expected.insert(expected.begin() +
                                    static_cast<std::ptrdiff_t>(target),
                                source);
                for (std::size_t destination = 0; destination < count;
                     ++destination) {
                    EXPECT(reorder_source_index(count, source, target,
                                                destination) ==
                           expected[destination]);
                }
            }
        }
    }

    EXPECT(!reorder_source_index(4, 4, 0, 0).has_value());
    EXPECT(!reorder_source_index(4, 0, 4, 0).has_value());
    EXPECT(!reorder_source_index(4, 0, 0, 4).has_value());
}

void test_tab_and_pane_mutations() {
    GroupState value = group();
    PaneState& first = value.panes.front();
    EXPECT(add_tab(first, tab("second", L"second-location")));
    EXPECT(set_active_tab(first, "second"));
    EXPECT(first.active_tab_id == "second");
    EXPECT(close_tab(first, "second", kDefault));
    EXPECT(first.active_tab_id == "tab-1");
    record_navigation(first.tabs.front(), kDefault);
    EXPECT(!first.tabs.front().history.empty());
    EXPECT(close_tab(first, "tab-1", kDefault));
    EXPECT(first.tabs.size() == 1);
    EXPECT(first.tabs.front().id == "tab-1");
    EXPECT(first.tabs.front().location == kDefault);
    EXPECT(first.tabs.front().history.empty());
    EXPECT(first.tabs.front().history_index == 0);
    EXPECT(is_valid(value));

    EXPECT(switch_layout(value, LayoutTemplate::left_right, kDefault,
                         {"pane-2"}, {"tab-2"}));
    EXPECT(set_active_pane(value, "pane-2"));
    EXPECT(value.active_pane_id == "pane-2");
    EXPECT(is_valid(value));
}

void test_reorder_tab() {
    GroupState value = group();
    PaneState& first = value.panes.front();
    EXPECT(add_tab(first, tab("second")));
    EXPECT(add_tab(first, tab("third")));
    first.active_tab_id = "second";

    EXPECT(reorder_tab(first, "tab-1", 2));
    EXPECT(first.tabs[0].id == "second");
    EXPECT(first.tabs[1].id == "third");
    EXPECT(first.tabs[2].id == "tab-1");
    EXPECT(first.active_tab_id == "second");

    const PaneState unchanged = first;
    EXPECT(!reorder_tab(first, "absent", 0));
    EXPECT(first == unchanged);
    EXPECT(!reorder_tab(first, "tab-1", first.tabs.size()));
    EXPECT(first == unchanged);
}

void test_move_tab() {
    PaneState source = pane("source", {tab("one", L"one"),
                                        tab("two", L"two")});
    source.active_tab_id = "one";
    PaneState target = pane("target", {tab("three", L"three")});

    EXPECT(move_tab(source, target, "one", 0, kDefault));
    EXPECT(source.tabs.size() == 1);
    EXPECT(source.active_tab_id == "two");
    EXPECT(target.tabs.front().id == "one");
    EXPECT(target.tabs.front().location.parsing_name == L"one");
    EXPECT(target.active_tab_id == "one");

    EXPECT(move_tab(source, target, "two", target.tabs.size(), kDefault));
    EXPECT(source.tabs.size() == 1);
    EXPECT(source.tabs.front().id == "two");
    EXPECT(source.tabs.front().location == kDefault);
    EXPECT(target.tabs.back().id == "two");
    EXPECT(target.tabs.back().location.parsing_name == L"two");
    EXPECT(target.active_tab_id == "two");
}

void test_layout_migration_stable_pane_identity() {
    GroupState value = group("group", LayoutTemplate::four_pane_grid);
    EXPECT(add_tab(value.panes[1], tab("tab-2b")));
    EXPECT(add_tab(value.panes[3], tab("tab-4b")));
    value.active_pane_id = "pane-4";

    // Shrinking to one pane never merges or discards panes: each identity
    // keeps its own tabs and only the visible count changes.
    EXPECT(switch_layout(value, LayoutTemplate::single, kDefault));
    EXPECT(value.panes.size() == 4);
    EXPECT(value.panes[0].tabs.size() == 1);
    EXPECT(value.panes[0].tabs.front().id == "tab-1");
    EXPECT(value.panes[1].tabs.size() == 2);
    EXPECT(value.panes[3].tabs.size() == 2);
    EXPECT(value.panes[3].tabs[1].id == "tab-4b");
    EXPECT(value.active_pane_id == "pane-1");  // old active now hidden -> first visible
    EXPECT(is_valid(value));

    // Growing back to four reveals the same identities with their tabs intact.
    EXPECT(switch_layout(value, LayoutTemplate::four_pane_grid, kDefault));
    EXPECT(value.panes.size() == 4);
    EXPECT(value.panes[3].tabs.size() == 2);
    EXPECT(value.panes[3].active_tab_id == "tab-4");
    EXPECT(value.divider_ratios == std::vector<double>({0.5, 0.5}));
    EXPECT(is_valid(value));
}

void test_layout_keeps_cross_pane_duplicate_tab_ids() {
    // A group is legal with the same tab id in different panes (ids are only
    // required unique within a pane). Because shrinking no longer merges panes,
    // these duplicates survive a shrink/grow round trip without making the
    // group invalid. Mirrors the "CPU MPT" session shape.
    GroupState value{"group-4", L"CPU MPT", LayoutTemplate::three_pane,
                     {0.5, 0.5}, {}, "pane-0"};
    value.panes.push_back(pane("pane-0", {tab("tab-2"), tab("tab-0"),
                                          tab("tab-1"), tab("tab-3"),
                                          tab("tab-4")}));
    value.panes.push_back(pane("pane-1", {tab("tab-1")}));
    value.panes.push_back(pane("pane-2", {tab("tab-2")}));
    EXPECT(is_valid(value));

    EXPECT(switch_layout(value, LayoutTemplate::single, kDefault));
    EXPECT(value.panes.size() == 3);
    EXPECT(is_valid(value));
    EXPECT(value.panes[0].tabs.size() == 5);
    EXPECT(value.panes[1].tabs.front().id == "tab-1");
    EXPECT(value.panes[2].tabs.front().id == "tab-2");

    EXPECT(switch_layout(value, LayoutTemplate::four_pane_grid, kDefault,
                         {"pane-3"}, {"tab-9"}));
    EXPECT(value.panes.size() == 4);
    EXPECT(is_valid(value));

    EXPECT(switch_layout(value, LayoutTemplate::left_right, kDefault));
    EXPECT(value.panes.size() == 4);
    EXPECT(is_valid(value));
}

void test_failed_mutations_leave_valid_state() {
    ApplicationState application{1, {group("one")}, "one", {},
                                 kDefaultSidebarWidth, {}};
    EXPECT(!add_group(application, group("one")));
    EXPECT(!duplicate_group(application, "one", "one", L"Duplicate"));
    EXPECT(!delete_group(application, "absent"));
    EXPECT(!reorder_group(application, "one", 1));

    GroupState value = group();
    EXPECT(!add_tab(value.panes.front(), tab("tab-1")));
    EXPECT(!close_tab(value.panes.front(), "absent", kDefault));
    EXPECT(!set_active_tab(value.panes.front(), "absent"));
    EXPECT(!set_active_pane(value, "absent"));
    EXPECT(!switch_layout(value, LayoutTemplate::four_pane_grid, kDefault,
                          {"only-one"}, {"only-one"}));
    EXPECT(is_valid(application));
    EXPECT(is_valid(value));
}

void test_pinned_location_deduplication() {
    ApplicationState application;
    EXPECT(add_pinned_location(application,
                               {L"\\\\server\\share", L"", L""}));
    EXPECT(!add_pinned_location(application,
                                {L"\\\\server\\share", L"different", L"other"}));
    EXPECT(application.pinned_locations.size() == 1);
    EXPECT(application.pinned_locations.front().known_folder_identity.empty());
}

}  // namespace

int main() {
    test_layout_metadata();
    test_invariants_reject_deliberate_breakage();
    test_tab_navigation_history();
    test_group_mutations();
    test_reorder_projection();
    test_tab_and_pane_mutations();
    test_reorder_tab();
    test_move_tab();
    test_layout_migration_stable_pane_identity();
    test_layout_keeps_cross_pane_duplicate_tab_ids();
    test_failed_mutations_leave_valid_state();
    test_pinned_location_deduplication();
    return panedock::test::summary("core_model");
}
