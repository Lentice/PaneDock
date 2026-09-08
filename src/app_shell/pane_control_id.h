#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace panedock::app_shell {

enum class PaneControl {
    tab_strip,
    back,
    forward,
    up,
    address_bar,
    refresh,
    view_mode,
    pinned,
    folder_context,
};

// PD-189: the id encodes only the kind of control. Every pane's proc knows
// which pane it is, so the pane index no longer needs a place in the id.
inline constexpr std::array<std::pair<PaneControl, int>, 9>
    kPaneControlIdBases{{
        {PaneControl::tab_strip, 200},
        {PaneControl::back, 300},
        {PaneControl::forward, 310},
        {PaneControl::up, 320},
        {PaneControl::address_bar, 330},
        {PaneControl::refresh, 340},
        {PaneControl::view_mode, 350},
        {PaneControl::pinned, 392},
        {PaneControl::folder_context, 790},
    }};

inline std::optional<PaneControl> decode_pane_control(int id) noexcept {
    for (const auto& [control, base] : kPaneControlIdBases)
        if (id == base) return control;
    return std::nullopt;
}

inline int encode_pane_control(PaneControl control) noexcept {
    for (const auto& [candidate, base] : kPaneControlIdBases)
        if (candidate == control) return base;
    return -1;
}

// ---------------------------------------------------------------------------
// Command id ranges.
//
// These used to be split across main.cpp and pane.h, with four call sites
// (WM_COMMAND routing, sidebar commands, WM_DRAWITEM, WM_MEASUREITEM) each
// re-deriving `id >= base && id < base + count` for the same blocks. A wrong
// constant in one of those copies is a silent misroute, so the ranges and the
// decode live here once, where pane_control_id_test can reach them.
// ---------------------------------------------------------------------------

// Panes are a fixed maximum; restated rather than including core/model.h so
// this header stays a leaf. main.cpp static_asserts it against
// core::kMaxPaneCount.
inline constexpr std::size_t kCommandPaneCount = 4;

// Sidebar: the Group list plus its six actions. The action ids double as the
// group list's context-menu command ids.
inline constexpr int kGroupListId = 100;
inline constexpr int kGroupActionIdBase = 101;
inline constexpr int kGroupActionCount = 6;

enum class GroupAction {
    create,
    duplicate,
    rename,
    remove,
    move_up,
    move_down,
};

inline constexpr int kNewGroupId = kGroupActionIdBase;
inline constexpr int kDuplicateGroupId = kGroupActionIdBase + 1;
inline constexpr int kRenameGroupId = kGroupActionIdBase + 2;
inline constexpr int kDeleteGroupId = kGroupActionIdBase + 3;
inline constexpr int kMoveUpId = kGroupActionIdBase + 4;
inline constexpr int kMoveDownId = kGroupActionIdBase + 5;

// View-mode popup commands: eight ids per pane, 360-391, kept separate from
// the navigation buttons and layout commands.
inline constexpr int kViewModeMenuIdBase = 360;
inline constexpr std::size_t kViewModeOptionCount = 8;
inline constexpr int kViewModeMenuIdCount =
    static_cast<int>(kCommandPaneCount * kViewModeOptionCount);

// Layout template buttons: one per fixed arrangement.
inline constexpr int kLayoutButtonIdBase = 400;
inline constexpr std::size_t kLayoutButtonCount = 8;

// Pinned popup commands: one block per pane, each with 64 custom locations,
// two fixed locations and two actions. The 500-771 range is separate from all
// controls.
inline constexpr int kPinnedMenuIdBase = 500;
inline constexpr int kPinnedMenuMaxLocationCount = 64;
inline constexpr int kPinnedMenuDesktopOffset = 0;
inline constexpr int kPinnedMenuThisPcOffset = 1;
inline constexpr int kPinnedMenuLocationOffset = 2;
inline constexpr int kPinnedMenuAddOffset =
    kPinnedMenuLocationOffset + kPinnedMenuMaxLocationCount;
inline constexpr int kPinnedMenuManageOffset = kPinnedMenuAddOffset + 1;
inline constexpr int kPinnedMenuSlotsPerPane = kPinnedMenuManageOffset + 1;
inline constexpr std::size_t kPinnedMenuFixedLocationCount = 2;
inline constexpr int kPinnedMenuIdCount =
    static_cast<int>(kCommandPaneCount) * kPinnedMenuSlotsPerPane;

// PD-113: fixed commands for the tab context menu, after all existing control
// and popup command ranges.
inline constexpr int kCloseTabId = 780;
inline constexpr int kCloseOtherTabsId = 781;
inline constexpr int kCloseAllTabsId = 782;
inline constexpr int kCloseTabsToRightId = 783;

enum class CommandKind {
    unknown,
    group_list,       // a selection in the sidebar's Group list
    group_action,     // `action`
    layout_template,  // `index` selects the arrangement
    view_mode,        // `pane`, `index` selects the option
    pinned_location,  // `pane`, `index` is the slot within that pane's block
    pinned_manage,    // `pane`
    tab_close,        // the four tab context-menu commands, `index` 0-3
    pane_control,     // `control`
};

struct Command final {
    CommandKind kind{CommandKind::unknown};
    std::size_t pane{};
    std::size_t index{};
    GroupAction action{};
    PaneControl control{};

    bool operator==(const Command&) const = default;
};

// Which block an id belongs to, and where in it. The ranges do not overlap;
// an id outside every block decodes to `unknown`.
inline Command decode_command(int id) noexcept {
    if (id == kGroupListId) return {CommandKind::group_list};
    if (id >= kGroupActionIdBase && id < kGroupActionIdBase + kGroupActionCount) {
        Command command{CommandKind::group_action};
        command.index = static_cast<std::size_t>(id - kGroupActionIdBase);
        command.action = static_cast<GroupAction>(command.index);
        return command;
    }
    if (const auto control = decode_pane_control(id)) {
        Command command{CommandKind::pane_control};
        command.control = *control;
        return command;
    }
    if (id >= kViewModeMenuIdBase &&
        id < kViewModeMenuIdBase + kViewModeMenuIdCount) {
        const int offset = id - kViewModeMenuIdBase;
        Command command{CommandKind::view_mode};
        command.pane = static_cast<std::size_t>(offset) / kViewModeOptionCount;
        command.index = static_cast<std::size_t>(offset) % kViewModeOptionCount;
        return command;
    }
    if (id >= kLayoutButtonIdBase &&
        id < kLayoutButtonIdBase + static_cast<int>(kLayoutButtonCount)) {
        Command command{CommandKind::layout_template};
        command.index = static_cast<std::size_t>(id - kLayoutButtonIdBase);
        return command;
    }
    if (id >= kPinnedMenuIdBase && id < kPinnedMenuIdBase + kPinnedMenuIdCount) {
        const int offset = id - kPinnedMenuIdBase;
        const int slot = offset % kPinnedMenuSlotsPerPane;
        Command command{slot == kPinnedMenuManageOffset
                            ? CommandKind::pinned_manage
                            : CommandKind::pinned_location};
        command.pane = static_cast<std::size_t>(offset / kPinnedMenuSlotsPerPane);
        command.index = static_cast<std::size_t>(slot);
        return command;
    }
    if (id >= kCloseTabId && id <= kCloseTabsToRightId) {
        Command command{CommandKind::tab_close};
        command.index = static_cast<std::size_t>(id - kCloseTabId);
        return command;
    }
    return {};
}

}  // namespace panedock::app_shell
