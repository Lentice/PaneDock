#include "core/model.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace panedock::core {
namespace {

template <typename Range, typename Id>
bool has_unique_ids(const Range& values, Id id) {
    std::unordered_set<std::string> ids;
    for (const auto& value : values) {
        if (value.id.empty() || !ids.insert(id(value)).second) {
            return false;
        }
    }
    return true;
}

template <typename Range>
auto find_id(Range& values, const std::string& id) {
    return std::find_if(values.begin(), values.end(), [&](const auto& value) {
        return value.id == id;
    });
}

// The merge path of switch_layout concatenates orphaned panes' tabs into a
// survivor pane. Tab ids are only required to be unique within a pane, so a
// group may legally carry the same tab id in different panes; merging then
// yields a duplicate id inside the survivor pane and flakes is_valid. Assign a
// fresh group-wide id to any colliding tab as it is appended.
std::string fresh_unique_tab_id(std::unordered_set<std::string>& used) {
    std::size_t index = 0;
    for (;;) {
        const std::string candidate = "tab-" + std::to_string(index++);
        if (used.insert(candidate).second) return candidate;
    }
}

}  // namespace

std::size_t pane_count(LayoutTemplate layout_template) noexcept {
    switch (layout_template) {
        case LayoutTemplate::single: return 1;
        case LayoutTemplate::left_right:
        case LayoutTemplate::top_bottom: return 2;
        case LayoutTemplate::three_pane: return 3;
        case LayoutTemplate::two_over_one: return 3;
        case LayoutTemplate::one_over_two: return 3;
        case LayoutTemplate::two_beside_one: return 3;
        case LayoutTemplate::four_pane_grid: return 4;
    }
    return 0;
}

std::size_t divider_ratio_count(LayoutTemplate layout_template) noexcept {
    switch (layout_template) {
        case LayoutTemplate::single: return 0;
        case LayoutTemplate::left_right:
        case LayoutTemplate::top_bottom: return 1;
        case LayoutTemplate::three_pane:
        case LayoutTemplate::two_over_one:
        case LayoutTemplate::one_over_two:
        case LayoutTemplate::two_beside_one:
        case LayoutTemplate::four_pane_grid: return 2;
    }
    return 0;
}

std::vector<double> default_divider_ratios(LayoutTemplate layout_template) {
    return std::vector<double>(divider_ratio_count(layout_template), 0.5);
}

void record_navigation(TabState& tab, ShellLocation location) {
    if (location == tab.location) return;
    if (tab.history.empty()) {
        tab.history.push_back(tab.location);
        tab.history_index = 0;
    }
    tab.history.erase(tab.history.begin() +
                          static_cast<std::ptrdiff_t>(tab.history_index + 1),
                      tab.history.end());
    tab.history.push_back(location);
    tab.history_index = tab.history.size() - 1;
    tab.location = std::move(location);
}

bool can_navigate_tab_back(const TabState& tab) noexcept {
    return !tab.history.empty() && tab.history_index > 0;
}

bool can_navigate_tab_forward(const TabState& tab) noexcept {
    return !tab.history.empty() && tab.history_index + 1 < tab.history.size();
}

bool navigate_tab_back(TabState& tab) noexcept {
    if (!can_navigate_tab_back(tab)) return false;
    tab.location = tab.history[--tab.history_index];
    return true;
}

bool navigate_tab_forward(TabState& tab) noexcept {
    if (!can_navigate_tab_forward(tab)) return false;
    tab.location = tab.history[++tab.history_index];
    return true;
}

bool is_valid(const GroupState& group) noexcept {
    if (group.id.empty() || group.panes.size() != pane_count(group.layout_template) ||
        group.divider_ratios.size() != divider_ratio_count(group.layout_template) ||
        !std::all_of(group.divider_ratios.begin(), group.divider_ratios.end(),
                     [](double ratio) {
                         return std::isfinite(ratio) && ratio >= 0.0 && ratio <= 1.0;
                     }) ||
        !has_unique_ids(group.panes, [](const PaneState& pane) {
            return pane.id;
        }) ||
        find_id(group.panes, group.active_pane_id) == group.panes.end()) {
        return false;
    }

    return std::all_of(group.panes.begin(), group.panes.end(),
                       [](const PaneState& pane) {
        return !pane.tabs.empty() &&
               has_unique_ids(pane.tabs, [](const TabState& tab) {
                   return tab.id;
               }) &&
               find_id(pane.tabs, pane.active_tab_id) != pane.tabs.end() &&
               std::all_of(pane.tabs.begin(), pane.tabs.end(),
                           [](const TabState& tab) {
                   return tab.history.empty() ||
                          (tab.history_index < tab.history.size() &&
                           tab.location == tab.history[tab.history_index]);
               });
    });
}

bool is_valid(const ApplicationState& application) noexcept {
    if (!has_unique_ids(application.groups, [](const GroupState& group) {
            return group.id;
        }) ||
        !std::all_of(application.groups.begin(), application.groups.end(),
                     [](const GroupState& group) { return is_valid(group); })) {
        return false;
    }
    return application.groups.empty()
               ? application.active_group_id.empty()
               : find_id(application.groups, application.active_group_id) !=
                     application.groups.end();
}

bool add_group(ApplicationState& application, GroupState group) {
    if (!is_valid(group) ||
        find_id(application.groups, group.id) != application.groups.end()) {
        return false;
    }
    application.groups.push_back(std::move(group));
    if (application.active_group_id.empty()) {
        application.active_group_id = application.groups.back().id;
    }
    return true;
}

bool rename_group(ApplicationState& application, const std::string& group_id,
                  std::wstring name) {
    const auto group = find_id(application.groups, group_id);
    if (group == application.groups.end()) {
        return false;
    }
    group->name = std::move(name);
    return true;
}

bool duplicate_group(ApplicationState& application,
                     const std::string& source_group_id,
                     std::string new_group_id, std::wstring new_name) {
    const auto source = find_id(application.groups, source_group_id);
    if (source == application.groups.end() || new_group_id.empty() ||
        find_id(application.groups, new_group_id) != application.groups.end()) {
        return false;
    }
    GroupState copy = *source;
    copy.id = std::move(new_group_id);
    copy.name = std::move(new_name);
    application.groups.push_back(std::move(copy));
    return true;
}

bool delete_group(ApplicationState& application, const std::string& group_id) {
    const auto group = find_id(application.groups, group_id);
    if (group == application.groups.end()) {
        return false;
    }
    const bool was_active = application.active_group_id == group_id;
    const auto next = application.groups.erase(group);
    if (application.groups.empty()) {
        application.active_group_id.clear();
    } else if (was_active) {
        application.active_group_id =
            (next == application.groups.end() ? application.groups.back() : *next).id;
    }
    return true;
}

bool reorder_group(ApplicationState& application, const std::string& group_id,
                   std::size_t new_index) {
    const auto group = find_id(application.groups, group_id);
    if (group == application.groups.end() || new_index >= application.groups.size()) {
        return false;
    }
    GroupState moved = std::move(*group);
    application.groups.erase(group);
    application.groups.insert(application.groups.begin() +
                                  static_cast<std::ptrdiff_t>(new_index),
                              std::move(moved));
    return true;
}

std::optional<std::size_t> reorder_source_index(
    std::size_t item_count, std::size_t source_index,
    std::size_t target_index, std::size_t destination_index) noexcept {
    if (source_index >= item_count || target_index >= item_count ||
        destination_index >= item_count) {
        return std::nullopt;
    }
    if (destination_index == target_index) return source_index;
    const std::size_t remaining_index =
        destination_index < target_index ? destination_index
                                         : destination_index - 1;
    return remaining_index < source_index ? remaining_index
                                          : remaining_index + 1;
}

bool switch_layout(GroupState& group, LayoutTemplate layout_template,
                   const ShellLocation& default_location,
                   const std::vector<std::string>& new_pane_ids,
                   const std::vector<std::string>& new_tab_ids) {
    if (!is_valid(group)) {
        return false;
    }
    const std::size_t old_count = group.panes.size();
    const std::size_t new_count = pane_count(layout_template);
    const std::size_t added = new_count > old_count ? new_count - old_count : 0;
    if (new_pane_ids.size() != added || new_tab_ids.size() != added) {
        return false;
    }

    GroupState candidate = group;

    if (new_count < old_count) {
        std::unordered_set<std::string> used_ids;
        for (const auto& pane : candidate.panes)
            for (const auto& tab : pane.tabs) used_ids.insert(tab.id);
        for (std::size_t index = new_count; index < old_count; ++index) {
            auto& destination =
                candidate.panes[(index - new_count) % new_count].tabs;
            for (auto& tab : candidate.panes[index].tabs) {
                if (std::any_of(destination.begin(), destination.end(),
                                [&](const TabState& existing) {
                                    return existing.id == tab.id;
                                })) {
                    tab.id = fresh_unique_tab_id(used_ids);
                }
                destination.push_back(std::move(tab));
            }
        }
        candidate.panes.resize(new_count);
    } else {
        for (std::size_t index = 0; index < added; ++index) {
            candidate.panes.push_back(PaneState{
                new_pane_ids[index],
                {TabState{new_tab_ids[index], default_location, {}, {}, true}},
                new_tab_ids[index]});
        }
    }

    candidate.layout_template = layout_template;
    candidate.divider_ratios = default_divider_ratios(layout_template);
    if (find_id(candidate.panes, candidate.active_pane_id) ==
        candidate.panes.end()) {
        candidate.active_pane_id = candidate.panes.front().id;
    }
    if (!is_valid(candidate)) {
        return false;
    }
    group = std::move(candidate);
    return true;
}

bool add_tab(PaneState& pane, TabState tab) {
    if (tab.id.empty() || find_id(pane.tabs, tab.id) != pane.tabs.end()) {
        return false;
    }
    pane.tabs.push_back(std::move(tab));
    return true;
}

bool reorder_tab(PaneState& pane, const std::string& tab_id,
                 std::size_t target_index) noexcept {
    const auto tab = find_id(pane.tabs, tab_id);
    if (tab == pane.tabs.end() || target_index >= pane.tabs.size()) {
        return false;
    }
    TabState moved = std::move(*tab);
    pane.tabs.erase(tab);
    pane.tabs.insert(pane.tabs.begin() +
                        static_cast<std::ptrdiff_t>(target_index),
                    std::move(moved));
    return true;
}

bool move_tab(PaneState& source, PaneState& target,
              const std::string& tab_id, std::size_t target_index,
              const ShellLocation& default_location) {
    const auto tab = find_id(source.tabs, tab_id);
    if (&source == &target || tab == source.tabs.end() ||
        find_id(target.tabs, tab_id) != target.tabs.end()) {
        return false;
    }

    TabState moved = *tab;
    if (source.tabs.size() == 1) {
        tab->location = default_location;
        tab->history.clear();
        tab->history_index = 0;
    } else {
        const bool was_active = source.active_tab_id == tab_id;
        const auto next = source.tabs.erase(tab);
        if (was_active) {
            source.active_tab_id =
                (next == source.tabs.end() ? source.tabs.back() : *next).id;
        }
    }
    target_index = std::min(target_index, target.tabs.size());
    target.tabs.insert(target.tabs.begin() +
                           static_cast<std::ptrdiff_t>(target_index),
                       std::move(moved));
    target.active_tab_id = tab_id;
    return true;
}

bool close_tab(PaneState& pane, const std::string& tab_id,
               const ShellLocation& default_location) {
    const auto tab = find_id(pane.tabs, tab_id);
    if (tab == pane.tabs.end()) {
        return false;
    }
    if (pane.tabs.size() == 1) {
        tab->location = default_location;
        tab->history.clear();
        tab->history_index = 0;
        return true;
    }
    const bool was_active = pane.active_tab_id == tab_id;
    const auto next = pane.tabs.erase(tab);
    if (was_active) {
        pane.active_tab_id =
            (next == pane.tabs.end() ? pane.tabs.back() : *next).id;
    }
    return true;
}

bool set_active_tab(PaneState& pane, const std::string& tab_id) noexcept {
    if (find_id(pane.tabs, tab_id) == pane.tabs.end()) {
        return false;
    }
    pane.active_tab_id = tab_id;
    return true;
}

bool set_active_pane(GroupState& group, const std::string& pane_id) noexcept {
    if (find_id(group.panes, pane_id) == group.panes.end()) {
        return false;
    }
    group.active_pane_id = pane_id;
    return true;
}

bool add_pinned_location(ApplicationState& application, ShellLocation location) {
    if (location.parsing_name.empty() ||
        std::find_if(application.pinned_locations.begin(),
                     application.pinned_locations.end(),
                     [&](const ShellLocation& pinned) {
                         return pinned.parsing_name == location.parsing_name;
                     }) != application.pinned_locations.end()) {
        return false;
    }
    application.pinned_locations.push_back(std::move(location));
    return true;
}

bool remove_pinned_location(ApplicationState& application,
                            std::size_t index) noexcept {
    if (index >= application.pinned_locations.size()) return false;
    application.pinned_locations.erase(
        application.pinned_locations.begin() +
        static_cast<std::ptrdiff_t>(index));
    return true;
}

bool reorder_pinned_location(ApplicationState& application,
                             std::size_t source_index,
                             std::size_t target_index) noexcept {
    if (source_index >= application.pinned_locations.size() ||
        target_index >= application.pinned_locations.size()) {
        return false;
    }
    ShellLocation moved = std::move(application.pinned_locations[source_index]);
    application.pinned_locations.erase(
        application.pinned_locations.begin() +
        static_cast<std::ptrdiff_t>(source_index));
    application.pinned_locations.insert(
        application.pinned_locations.begin() +
            static_cast<std::ptrdiff_t>(target_index),
        std::move(moved));
    return true;
}

}  // namespace panedock::core
