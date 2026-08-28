#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace panedock::core {

inline constexpr int kDefaultSidebarWidth = 194;

struct ShellLocation final {
    std::wstring parsing_name;
    std::wstring known_folder_identity;
    std::wstring fallback_path;

    bool operator==(const ShellLocation&) const = default;
};

enum class LayoutTemplate {
    single,
    left_right,
    top_bottom,
    three_pane,
    four_pane_grid,
    two_over_one,
};

struct TabState final {
    std::string id;
    ShellLocation location;
    std::string view_mode;
    std::string sort_column;
    bool sort_ascending{true};
    std::vector<ShellLocation> history;
    std::size_t history_index{0};

    bool operator==(const TabState&) const = default;
};

struct PaneState final {
    std::string id;
    std::vector<TabState> tabs;
    std::string active_tab_id;

    bool operator==(const PaneState&) const = default;
};

struct GroupState final {
    std::string id;
    std::wstring name;
    LayoutTemplate layout_template{LayoutTemplate::single};
    std::vector<double> divider_ratios;
    std::vector<PaneState> panes;
    std::string active_pane_id;

    bool operator==(const GroupState&) const = default;
};

struct ApplicationState final {
    struct WindowPlacement final {
        int x{};
        int y{};
        int width{};
        int height{};
        bool maximized{};

        bool operator==(const WindowPlacement&) const = default;
    };

    std::uint32_t schema_version{1};
    std::vector<GroupState> groups;
    std::string active_group_id;  // Empty exactly when groups is empty.
    WindowPlacement window_placement;
    int sidebar_width{kDefaultSidebarWidth};
    std::vector<ShellLocation> pinned_locations;

    bool operator==(const ApplicationState&) const = default;
};

std::size_t pane_count(LayoutTemplate layout_template) noexcept;
std::size_t divider_ratio_count(LayoutTemplate layout_template) noexcept;
std::vector<double> default_divider_ratios(LayoutTemplate layout_template);

void record_navigation(TabState& tab, ShellLocation location);
bool can_navigate_tab_back(const TabState& tab) noexcept;
bool can_navigate_tab_forward(const TabState& tab) noexcept;
bool navigate_tab_back(TabState& tab) noexcept;
bool navigate_tab_forward(TabState& tab) noexcept;

bool is_valid(const GroupState& group) noexcept;
bool is_valid(const ApplicationState& application) noexcept;

bool add_group(ApplicationState& application, GroupState group);
bool rename_group(ApplicationState& application, const std::string& group_id,
                  std::wstring name);
bool duplicate_group(ApplicationState& application,
                     const std::string& source_group_id,
                     std::string new_group_id, std::wstring new_name);
bool delete_group(ApplicationState& application, const std::string& group_id);
bool reorder_group(ApplicationState& application, const std::string& group_id,
                   std::size_t new_index);

bool switch_layout(GroupState& group, LayoutTemplate layout_template,
                   const ShellLocation& default_location,
                   const std::vector<std::string>& new_pane_ids = {},
                   const std::vector<std::string>& new_tab_ids = {});

bool add_tab(PaneState& pane, TabState tab);
bool reorder_tab(PaneState& pane, const std::string& tab_id,
                 std::size_t target_index) noexcept;
bool move_tab(PaneState& source, PaneState& target,
              const std::string& tab_id, std::size_t target_index,
              const ShellLocation& default_location);
bool close_tab(PaneState& pane, const std::string& tab_id,
               const ShellLocation& default_location);
bool set_active_tab(PaneState& pane, const std::string& tab_id) noexcept;
bool set_active_pane(GroupState& group, const std::string& pane_id) noexcept;
bool add_pinned_location(ApplicationState& application, ShellLocation location);

}  // namespace panedock::core
