#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace panedock::core {

inline constexpr int kDefaultSidebarWidth = 194;
inline constexpr std::size_t kMaxPaneCount = 4;

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
    one_over_two,
    two_beside_one,
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

// core's contract is that a PaneState address stays stable for the lifetime
// of the GroupState that owns it. TabState addresses are not covered because
// PaneState::tabs may reallocate.
struct GroupState final {
    std::string id;
    std::wstring name;
    LayoutTemplate layout_template{LayoutTemplate::single};
    std::vector<double> divider_ratios;
    std::vector<PaneState> panes;
    std::string active_pane_id;

    bool operator==(const GroupState&) const = default;
};

// app_shell::Pane holds a raw core::PaneState* for the lifetime of the
// group it displays (PD-185). Reserving the fixed maximum up front is what
// makes that pointer safe: switch_layout only ever push_backs (model.cpp
// "Panes are stable identities and are never discarded ... on a shrink"),
// so with capacity kMaxPaneCount the vector never reallocates and no
// PaneState is ever relocated.
inline void reserve_panes(GroupState& group) {
    group.panes.reserve(kMaxPaneCount);
}

// std::vector relocates elements with move-if-noexcept. If GroupState ever
// gains a member whose move can throw, the groups vector silently falls
// back to copying, which relocates every PaneState and dangles every
// pointer app_shell::Pane holds. Keep this assertion true.
static_assert(std::is_nothrow_move_constructible_v<GroupState>);

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

// The only source of persisted Group and tab identity. Both are pure so the
// collision rules can be tested: an id that escapes into the session document
// and collides there is unrecoverable by a later read.
//
// Numeric ids get max+1. An id that is not "group-<digits>" cannot be ordered,
// so the whole allocation falls back to a millisecond timestamp plus a
// discriminator loop that checks the actual group list.
std::string next_group_id(const ApplicationState& application);

// `candidate_index` is an in/out cursor so one caller allocating several ids
// for the same Group does not hand out the same id twice: core::move_tab
// requires the retained placeholder id to be free across the whole Group.
std::string next_tab_id(const GroupState& group,
                        std::size_t& candidate_index);

bool add_group(ApplicationState& application, GroupState group);
bool rename_group(ApplicationState& application, const std::string& group_id,
                  std::wstring name);
bool duplicate_group(ApplicationState& application,
                     const std::string& source_group_id,
                     std::string new_group_id, std::wstring new_name);
bool delete_group(ApplicationState& application, const std::string& group_id);
bool reorder_group(ApplicationState& application, const std::string& group_id,
                   std::size_t new_index);
std::optional<std::size_t> reorder_source_index(
    std::size_t item_count, std::size_t source_index,
    std::size_t target_index, std::size_t destination_index) noexcept;

bool switch_layout(GroupState& group, LayoutTemplate layout_template,
                   const ShellLocation& default_location,
                   const std::vector<std::string>& new_pane_ids = {},
                   const std::vector<std::string>& new_tab_ids = {});

bool add_tab(PaneState& pane, TabState tab);
bool reorder_tab(PaneState& pane, const std::string& tab_id,
                 std::size_t target_index) noexcept;
// Moves one tab between panes. When `source` holds only that tab it keeps a
// reset placeholder rather than becoming empty; `retained_tab_id` is the id
// that placeholder takes, and must be unused anywhere in the group -- reusing
// `tab_id` would leave the same identity in two panes, which later blocks
// moving the tab back and persists a duplicate id into the session.
bool move_tab(PaneState& source, PaneState& target,
              const std::string& tab_id, std::size_t target_index,
              const ShellLocation& default_location,
              const std::string& retained_tab_id);
bool close_tab(PaneState& pane, const std::string& tab_id,
               const ShellLocation& default_location);
bool set_active_tab(PaneState& pane, const std::string& tab_id) noexcept;
bool set_active_pane(GroupState& group, const std::string& pane_id) noexcept;
bool add_pinned_location(ApplicationState& application, ShellLocation location);
bool remove_pinned_location(ApplicationState& application,
                            std::size_t index) noexcept;
bool reorder_pinned_location(ApplicationState& application,
                             std::size_t source_index,
                             std::size_t target_index) noexcept;

}  // namespace panedock::core
