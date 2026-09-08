#include "core/group_transition.h"
#include "unit/test_util.h"

#include <vector>

namespace {
using namespace panedock::core;
using Step = GroupTransitionStep;

using Steps = std::vector<GroupTransitionStep>;

// The whole point of this module is that the *order* is a value, not four
// hand-copied statement sequences. So the tests assert the exact sequence.
const Steps kFullScript{
    Step::rebind_panes,      Step::refresh_tab_strips,
    Step::navigate_realized_panes, Step::apply_layout,
    Step::focus_active_pane, Step::refresh_sidebar,
    Step::save_session};

void test_activating_a_group_runs_the_full_script() {
    EXPECT(plan_group_transition(GroupTransition::activate, true, true) ==
           kFullScript);
}

// A layout change moves no pane anywhere: apply_layout realizes whatever the
// new template added, and the panes that stay are already where they belong.
void test_a_relayout_skips_the_re_navigation() {
    const Steps expected{Step::rebind_panes,      Step::refresh_tab_strips,
                         Step::apply_layout,      Step::focus_active_pane,
                         Step::refresh_sidebar,   Step::save_session};
    EXPECT(plan_group_transition(GroupTransition::relayout, true, false) ==
           expected);
}

// This is the divergence the consolidation found: set_layout never refreshed
// the sidebar, but the Group summary counts only the panes the current layout
// shows, so the row went stale after every layout change.
void test_every_transition_refreshes_the_sidebar() {
    for (const auto transition :
         {GroupTransition::activate, GroupTransition::relayout,
          GroupTransition::remove}) {
        for (const bool changed : {false, true}) {
            const auto steps = plan_group_transition(transition, true, changed);
            bool found = false;
            for (const auto step : steps)
                if (step == Step::refresh_sidebar) found = true;
            EXPECT(found);
            EXPECT(steps.back() == Step::save_session);
            EXPECT(steps.front() == Step::rebind_panes);
        }
    }
}

void test_deleting_the_active_group_re_navigates_but_an_inactive_one_does_not() {
    EXPECT(plan_group_transition(GroupTransition::remove, true, true) ==
           kFullScript);

    const Steps unchanged{Step::rebind_panes,    Step::refresh_tab_strips,
                          Step::apply_layout,    Step::focus_active_pane,
                          Step::refresh_sidebar, Step::save_session};
    EXPECT(plan_group_transition(GroupTransition::remove, true, false) ==
           unchanged);
}

// Deleting the last Group leaves nothing to navigate to or focus, but the
// sidebar and the session still have to be told.
void test_the_empty_state_still_saves_and_refreshes() {
    const Steps expected{Step::rebind_panes, Step::refresh_tab_strips,
                         Step::apply_layout, Step::refresh_sidebar,
                         Step::save_session};
    EXPECT(plan_group_transition(GroupTransition::remove, false, true) ==
           expected);
    // Not even an "activate" can focus a pane that is not there.
    EXPECT(plan_group_transition(GroupTransition::activate, false, true) ==
           expected);
}

// Every plan rebinds before anything reads a PaneState, and applies the
// layout before focusing a pane that layout may have just created.
void test_the_ordering_invariants_hold_for_every_plan() {
    for (const auto transition :
         {GroupTransition::activate, GroupTransition::relayout,
          GroupTransition::remove}) {
        for (const bool has_group : {false, true}) {
            for (const bool changed : {false, true}) {
                const auto steps =
                    plan_group_transition(transition, has_group, changed);
                std::size_t rebind = 0, apply = 0, focus = steps.size();
                std::size_t navigate = steps.size();
                for (std::size_t i = 0; i < steps.size(); ++i) {
                    if (steps[i] == Step::rebind_panes) rebind = i;
                    if (steps[i] == Step::apply_layout) apply = i;
                    if (steps[i] == Step::focus_active_pane) focus = i;
                    if (steps[i] == Step::navigate_realized_panes) navigate = i;
                }
                EXPECT(rebind < apply);
                EXPECT(apply < focus || focus == steps.size());
                EXPECT(navigate < apply || navigate == steps.size());
                // Nothing is scheduled twice.
                for (std::size_t i = 0; i < steps.size(); ++i)
                    for (std::size_t j = i + 1; j < steps.size(); ++j)
                        EXPECT(steps[i] != steps[j]);
                // No pane means no pane-touching step.
                if (!has_group) {
                    EXPECT(focus == steps.size());
                    EXPECT(navigate == steps.size());
                }
            }
        }
    }
}
}  // namespace

int main() {
    test_activating_a_group_runs_the_full_script();
    test_a_relayout_skips_the_re_navigation();
    test_every_transition_refreshes_the_sidebar();
    test_deleting_the_active_group_re_navigates_but_an_inactive_one_does_not();
    test_the_empty_state_still_saves_and_refreshes();
    test_the_ordering_invariants_hold_for_every_plan();
    return panedock::test::summary("core_group_transition");
}
