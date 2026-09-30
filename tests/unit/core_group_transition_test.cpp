#include "core/group_transition.h"
#include "unit/test_util.h"

#include <string>
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

// PD-206 rebinds the panes at rebind_panes but the live views only follow at
// navigate_realized_panes, so the coordinator suppresses location capture from
// the first step. If rebind ever stopped preceding navigate, that suppression
// would be guarding the wrong window.
void test_rebind_precedes_navigation_so_the_capture_guard_covers_the_gap() {
    for (const bool has_group : {true, false}) {
        for (const bool changed : {true, false}) {
            for (const auto transition :
                 {GroupTransition::activate, GroupTransition::relayout,
                  GroupTransition::remove}) {
                const auto steps =
                    plan_group_transition(transition, has_group, changed);
                std::size_t rebind = steps.size();
                std::size_t navigate = steps.size();
                for (std::size_t i = 0; i < steps.size(); ++i) {
                    if (steps[i] == Step::rebind_panes) rebind = i;
                    if (steps[i] == Step::navigate_realized_panes) navigate = i;
                }
                EXPECT(rebind != steps.size());
                EXPECT(navigate == steps.size() || rebind < navigate);
                // save_session is last, so releasing the suppression there
                // cannot leave a later step unguarded.
                if (!steps.empty())
                    EXPECT(steps.back() == Step::save_session);
            }
        }
    }
}

// run_group_transition's output is the effects it performs, in order.
class RecordingEffects final : public GroupTransitionEffects {
  public:
    std::vector<std::string> log;
    bool shutting_down{};
    bool active_group{true};
    bool navigation_succeeds{true};
    // Name of the effect after which shutdown starts, as a nested close
    // dispatched inside that step's pump would.
    std::string shutdown_after;
    // Name of the effect after which the active Group is gone, as a queued
    // delete_group replayed inside that step's pump would leave it.
    std::string vanish_after;

    bool is_shutting_down() const noexcept override { return shutting_down; }
    bool has_active_group() const noexcept override { return active_group; }
    void rebind_panes() override { record("rebind"); }
    void refresh_tab_strips() override { record("tabs"); }
    bool navigate_realized_panes() override {
        record("navigate");
        return navigation_succeeds;
    }
    void apply_layout() override { record("layout"); }
    void focus_active_pane() override { record("focus"); }
    void refresh_sidebar() override { record("sidebar"); }
    void release_capture_suppression() noexcept override { record("release"); }
    void schedule_session_save() noexcept override { record("save"); }

  private:
    void record(const char *name) {
        log.push_back(name);
        if (shutdown_after == name) shutting_down = true;
        if (vanish_after == name) active_group = false;
    }
};

using Log = std::vector<std::string>;

void test_a_switch_runs_every_effect_and_releases_before_the_save() {
    RecordingEffects effects;
    run_group_transition(GroupTransition::activate, "group-a", "group-b",
                         effects);
    EXPECT((effects.log == Log{"rebind", "tabs", "navigate", "layout", "focus",
                               "sidebar", "release", "save"}));
}

void test_a_shutdown_inside_a_step_stops_the_rest_and_still_releases() {
    RecordingEffects effects;
    effects.shutdown_after = "tabs";
    run_group_transition(GroupTransition::activate, "group-a", "group-b",
                         effects);
    EXPECT((effects.log == Log{"rebind", "tabs", "release"}));
}

void test_a_failed_navigation_aborts_before_the_layout() {
    RecordingEffects effects;
    effects.navigation_succeeds = false;
    run_group_transition(GroupTransition::activate, "group-a", "group-b",
                         effects);
    // The refused save is owed; SessionWriter reschedules it on release.
    EXPECT((effects.log == Log{"rebind", "tabs", "navigate", "release"}));
}

void test_a_transition_that_starts_during_shutdown_does_nothing_but_release() {
    RecordingEffects effects;
    effects.shutting_down = true;
    run_group_transition(GroupTransition::activate, "group-a", "group-b",
                         effects);
    EXPECT((effects.log == Log{"release"}));
}

void test_the_changed_group_is_derived_from_the_ids() {
    RecordingEffects kept;
    run_group_transition(GroupTransition::remove, "group-a", "group-a", kept);
    EXPECT((kept.log == Log{"rebind", "tabs", "layout", "focus", "sidebar",
                            "release", "save"}));
    RecordingEffects moved;
    run_group_transition(GroupTransition::remove, "group-a", "group-b", moved);
    EXPECT(moved.log.size() == 8 && moved.log[2] == "navigate");
}

void test_a_group_removed_mid_transition_skips_navigation_and_focus() {
    RecordingEffects effects;
    effects.vanish_after = "tabs";
    run_group_transition(GroupTransition::activate, "group-a", "group-b",
                         effects);
    EXPECT((effects.log ==
            Log{"rebind", "tabs", "layout", "sidebar", "release", "save"}));
}
}  // namespace

int main() {
    test_a_switch_runs_every_effect_and_releases_before_the_save();
    test_a_shutdown_inside_a_step_stops_the_rest_and_still_releases();
    test_a_failed_navigation_aborts_before_the_layout();
    test_a_transition_that_starts_during_shutdown_does_nothing_but_release();
    test_the_changed_group_is_derived_from_the_ids();
    test_a_group_removed_mid_transition_skips_navigation_and_focus();
    test_activating_a_group_runs_the_full_script();
    test_a_relayout_skips_the_re_navigation();
    test_every_transition_refreshes_the_sidebar();
    test_deleting_the_active_group_re_navigates_but_an_inactive_one_does_not();
    test_the_empty_state_still_saves_and_refreshes();
    test_the_ordering_invariants_hold_for_every_plan();
    test_rebind_precedes_navigation_so_the_capture_guard_covers_the_gap();
    return panedock::test::summary("core_group_transition");
}
