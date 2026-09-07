#pragma once

// The test adapter for the PaneHost seam.
//
// PaneHost has two adapters: AppState in the app, and this one in the tests.
// It used to be written out once per test file, so adding one PaneHost method
// meant three edits and the compiler named the third only after the second was
// fixed. Every default here is inert; a test overrides the public field for
// the one answer it is about.

#include "app_shell/pane.h"
#include "app_shell/pane_host.h"

namespace panedock::test {

class TestPaneHost : public panedock::app_shell::PaneHost {
  public:
    bool shutting_down{};
    std::string group_id{"group-a"};
    bool suppress_location_capture{};
    int session_saves{};
    // Set to a bound PaneState to have the second display-name lookup mutate
    // the tab list, standing in for a Shell call that re-enters and changes
    // the model mid-scan.
    panedock::core::PaneState* mutate_on_second_lookup{};
    int lookups{};

    bool is_shutting_down() const noexcept override { return shutting_down; }
    void shell_call_entered() noexcept override {}
    void shell_call_left() noexcept override {}
    void schedule_session_save() noexcept override { ++session_saves; }
    const std::string& active_group_id() const noexcept override {
        return group_id;
    }
    std::string make_unique_tab_id() const override { return "tab-new"; }
    std::optional<panedock::app_shell::TabStripDragLayout> tab_drag_layout(
        const panedock::app_shell::Pane&, HWND, int, int, int) const override {
        return std::nullopt;
    }
    std::span<const panedock::app_shell::PinnedLocation> pinned_locations()
        const noexcept override {
        return {};
    }
    void pin_location(panedock::core::ShellLocation) override {}
    // No test here pumps pane control messages, so this answers "none of the
    // pane's business".
    std::optional<LRESULT> handle_pane_control_message(
        panedock::app_shell::Pane&, UINT, WPARAM, LPARAM) override {
        return std::nullopt;
    }
    bool location_capture_suppressed() const noexcept override {
        return suppress_location_capture;
    }
    std::wstring tab_display_text(std::wstring_view name) override {
        if (++lookups == 2 && mutate_on_second_lookup != nullptr)
            mutate_on_second_lookup->tabs.erase(
                mutate_on_second_lookup->tabs.begin());
        return std::wstring(name);
    }
    HFONT chrome_font() const noexcept override {
        return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    HWND tooltip() const noexcept override { return nullptr; }
};

}  // namespace panedock::test
