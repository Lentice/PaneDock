#include "unit/test_util.h"

#include "app_shell/layout_state.h"
#include "explorer_host/live_view_count.h"

#include <array>

int main() {
    using panedock::app_shell::LayoutState;
    using panedock::app_shell::LayoutTemplate;
    using panedock::explorer_host::LiveViewRegistration;
    using panedock::explorer_host::live_view_count;

    std::array<LiveViewRegistration, LayoutState::kPaneCount> views;
    for (auto& view : views) {
        view.mark_initialized();
    }

    LayoutState state;
    EXPECT(state.layout() == LayoutTemplate::four_pane);
    EXPECT(state.active_pane() == 0);
    EXPECT(live_view_count() == LayoutState::kPaneCount);

    const std::array<std::size_t, 8> active_panes{3, 0, 2, 1, 3, 0, 1, 0};
    for (const std::size_t pane : active_panes) {
        if (state.layout() == LayoutTemplate::two_pane && pane >= 2) {
            EXPECT(!state.set_active_pane(pane));
        } else {
            EXPECT(state.set_active_pane(pane));
        }
        state.toggle_layout();
        EXPECT(state.active_pane() < LayoutState::kPaneCount);
        EXPECT(live_view_count() == LayoutState::kPaneCount);
        state.toggle_layout();
        EXPECT(state.active_pane() < LayoutState::kPaneCount);
        EXPECT(live_view_count() == LayoutState::kPaneCount);
    }

    EXPECT(state.set_active_pane(3));
    state.toggle_layout();
    EXPECT(state.layout() == LayoutTemplate::two_pane);
    EXPECT(state.active_pane() == 0);
    EXPECT(!state.set_active_pane(3));
    EXPECT(state.active_pane() == 0);
    EXPECT(live_view_count() == LayoutState::kPaneCount);
    state.toggle_layout();
    EXPECT(state.layout() == LayoutTemplate::four_pane);
    EXPECT(state.active_pane() == 0);
    EXPECT(live_view_count() == LayoutState::kPaneCount);

    return panedock::test::summary("layout_state_check");
}
