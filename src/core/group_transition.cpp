#include "core/group_transition.h"

namespace panedock::core {

std::vector<GroupTransitionStep> plan_group_transition(
    GroupTransition transition, bool has_active_group,
    bool active_group_changed) {
    std::vector<GroupTransitionStep> steps;
    steps.reserve(7);

    // Panes bind to the surviving Group's PaneState before anything reads it.
    steps.push_back(GroupTransitionStep::rebind_panes);
    steps.push_back(GroupTransitionStep::refresh_tab_strips);

    // Only a Group swap moves the panes somewhere else. A layout change adds
    // or hides panes, which apply_layout realizes; the panes that stay are
    // already at the right location.
    const bool moved = has_active_group &&
                       (transition == GroupTransition::activate ||
                        (transition == GroupTransition::remove &&
                         active_group_changed));
    if (moved) steps.push_back(GroupTransitionStep::navigate_realized_panes);

    steps.push_back(GroupTransitionStep::apply_layout);
    if (has_active_group)
        steps.push_back(GroupTransitionStep::focus_active_pane);
    // The sidebar summary counts only the panes the current layout shows, so
    // a relayout changes it just as much as a Group swap does.
    steps.push_back(GroupTransitionStep::refresh_sidebar);
    steps.push_back(GroupTransitionStep::save_session);
    return steps;
}

}  // namespace panedock::core
