#pragma once

#include <vector>

namespace panedock::core {

// What changed about the Group arrangement. The mutation itself has already
// been applied to the model; this describes which one it was.
enum class GroupTransition {
    activate,  // a different Group became the active one, or the first was added
    relayout,  // the active Group changed layout template
    remove,    // a Group was deleted
};

// The ordered script every Group mutation runs afterwards. It used to be
// hand-copied into activate_group, add_group, delete_group and set_layout,
// and the copies had already drifted apart with nothing able to see it.
enum class GroupTransitionStep {
    rebind_panes,
    refresh_tab_strips,
    navigate_realized_panes,
    apply_layout,
    focus_active_pane,
    refresh_sidebar,
    save_session,
};

// `active_group_changed` means the transition left a different Group active
// than the one that was showing, which is what decides whether the realized
// panes have to be re-navigated. A `remove` that deleted an inactive Group
// leaves every pane pointing where it already was.
std::vector<GroupTransitionStep> plan_group_transition(
    GroupTransition transition, bool has_active_group,
    bool active_group_changed);

}  // namespace panedock::core
