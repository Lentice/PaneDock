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
#include "app_shell/session_writer.h"
#include "core/key_routing.h"
#include "core/shutdown.h"
#include "unit/test_reentry_effects.h"

namespace panedock::test {

class TestPaneHost : public panedock::app_shell::PaneHost {
    // A real managed list registers an OLE drop target, even when enumeration
    // and transfer providers are replaced by deterministic value harnesses.
    struct OleApartment final {
        HRESULT result{OleInitialize(nullptr)};
        ~OleApartment() { if (SUCCEEDED(result)) OleUninitialize(); }
    } ole_;
  public:
    bool shutting_down{};
    std::string group_id{"group-a"};
    int session_saves{};
    // Set to a bound PaneState to have the second display-name lookup mutate
    // the tab list, standing in for a Shell call that re-enters and changes
    // the model mid-scan.
    panedock::core::PaneState* mutate_on_second_lookup{};
    int lookups{};
    // A real guard, so a pane under test goes through real Shell re-entry
    // bookkeeping; tests hand it to the pane with set_reentry_guard.
    panedock::core::ShutdownSequence shutdown;
    RecordingReentryEffects reentry_effects;
    panedock::app_shell::ShellReentryGuard guard{shutdown, reentry_effects,
                                                 WM_APP};
    // Owns the capture gate a pane asks; hold a CaptureSuppression on it to
    // stand in for a Group transition.
    panedock::app_shell::SessionWriter session;

    bool is_shutting_down() const noexcept override { return shutting_down; }
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
    bool diagnostic_timing_enabled() const noexcept override { return false; }
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

// What the message loop routes a key to while this pane is the active one:
// the same core::KeyInput main.cpp builds, with modifiers given rather than
// read from GetKeyState and address-bar focus taken from the message target
// (or a list rename edit holding focus, as main.cpp counts it).
inline panedock::core::KeyAction route_list_key(
    const panedock::app_shell::Pane& pane, const MSG& message,
    bool control = false, bool alt = false, bool shift = false) {
    panedock::core::KeyInput input{};
    input.message = message.message == WM_KEYDOWN
        ? panedock::core::KeyMessage::key_down
        : (message.message == WM_SYSKEYDOWN
               ? panedock::core::KeyMessage::system_key_down
               : panedock::core::KeyMessage::other);
    input.virtual_key = static_cast<unsigned>(message.wParam);
    input.control = control;
    input.alt = alt;
    input.shift = shift;
    input.address_bar_focused = (message.hwnd != nullptr &&
        message.hwnd == pane.address_bar()) || pane.list_edit_has_focus();
    input.list_focused = pane.list_has_key(message);
    input.pane_count = 1;
    return panedock::core::resolve_key(input).action;
}

// Routes the key and lets the pane perform it; true when the pane consumed it.
inline bool press_list_key(panedock::app_shell::Pane& pane, const MSG& message,
                           bool control = false, bool alt = false,
                           bool shift = false) {
    return pane.perform_list_key(
        route_list_key(pane, message, control, alt, shift));
}

}  // namespace panedock::test
