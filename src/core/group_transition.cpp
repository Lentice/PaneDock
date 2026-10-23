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

void run_group_transition(GroupTransition transition,
                          std::string_view previous_active_group_id,
                          std::string_view active_group_id,
                          GroupTransitionEffects &effects) {
    const bool changed = previous_active_group_id != active_group_id;
    for (const GroupTransitionStep step : plan_group_transition(
             transition, effects.has_active_group(), changed)) {
        if (effects.is_shutting_down()) return;
        switch (step) {
            case GroupTransitionStep::rebind_panes:
                effects.rebind_panes();
                break;
            case GroupTransitionStep::refresh_tab_strips:
                effects.refresh_tab_strips();
                break;
            case GroupTransitionStep::navigate_realized_panes:
                if (!effects.has_active_group()) break;
                if (!effects.navigate_realized_panes()) return;
                break;
            case GroupTransitionStep::apply_layout:
                effects.apply_layout();
                break;
            case GroupTransitionStep::focus_active_pane:
                if (!effects.has_active_group()) break;
                effects.focus_active_pane();
                break;
            case GroupTransitionStep::refresh_sidebar:
                effects.refresh_sidebar();
                break;
            case GroupTransitionStep::save_session:
                effects.schedule_session_save();
                break;
        }
    }
}

}  // namespace panedock::core
