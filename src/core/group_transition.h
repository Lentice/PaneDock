#pragma once

#include <string_view>
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

// The effects each step performs. The app adapter lives in main.cpp next to
// perform_group_transition; tests use a recording fake. Every effect that can
// pump the message loop is followed by a fresh is_shutting_down() check.
class GroupTransitionEffects {
  public:
    virtual ~GroupTransitionEffects() = default;
    virtual bool is_shutting_down() const noexcept = 0;
    // Asked again before each step that needs it: a step that pumps the loop
    // can let a queued delete_group remove the Group underneath.
    virtual bool has_active_group() const noexcept = 0;
    virtual void rebind_panes() = 0;
    virtual void refresh_tab_strips() = 0;
    // False aborts the transition: a failed navigation must not be followed
    // by a layout that shows panes still pointing at the outgoing Group.
    virtual bool navigate_realized_panes() = 0;
    virtual void apply_layout() = 0;
    virtual void focus_active_pane() = 0;
    virtual void refresh_sidebar() = 0;
    // Ends the PD-206 capture suppression the caller took for this
    // transition. Called exactly once, on every exit, and before the save.
    virtual void release_capture_suppression() noexcept = 0;
    virtual void schedule_session_save() noexcept = 0;
};

// Runs plan_group_transition against the effects. Whether the active Group
// changed is derived from the ids rather than asserted by each caller.
void run_group_transition(GroupTransition transition,
                          std::string_view previous_active_group_id,
                          std::string_view active_group_id,
                          GroupTransitionEffects &effects);

}  // namespace panedock::core
