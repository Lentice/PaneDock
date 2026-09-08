#include "core/key_routing.h"
#include "unit/test_util.h"

#include <initializer_list>

namespace {
using namespace panedock::core;

// A four-pane Group with pane 1 active and nothing else focused.
KeyInput key(unsigned virtual_key,
             KeyMessage message = KeyMessage::key_down) {
    KeyInput input{};
    input.message = message;
    input.virtual_key = virtual_key;
    input.active_pane = 1;
    input.pane_count = 4;
    return input;
}

// This is the rule that regressed before: plain Tab must reach pane
// navigation, not the Shell view's column header.
void test_plain_tab_beats_the_shell_accelerator() {
    const auto forward = resolve_key(key(kVirtualKeyTab));
    EXPECT(forward.action == KeyAction::focus_next_pane);
    EXPECT(forward.shell == ShellAccelerator::never);
    EXPECT(forward.target_pane == 2);

    auto shifted = key(kVirtualKeyTab);
    shifted.shift = true;
    const auto backward = resolve_key(shifted);
    EXPECT(backward.action == KeyAction::focus_previous_pane);
    EXPECT(backward.shell == ShellAccelerator::never);
    EXPECT(backward.target_pane == 0);

    // Alt+Tab and Ctrl+Tab are not pane navigation.
    auto alt_tab = key(kVirtualKeyTab);
    alt_tab.alt = true;
    EXPECT(resolve_key(alt_tab).action == KeyAction::none);
}

void test_pane_cycle_wraps_in_both_directions() {
    auto last = key(kVirtualKeyTab);
    last.active_pane = 3;
    EXPECT(resolve_key(last).target_pane == 0);

    auto first = key(kVirtualKeyTab);
    first.active_pane = 0;
    first.shift = true;
    EXPECT(resolve_key(first).target_pane == 3);

    // A single-pane layout stays on the one pane it has.
    auto single = key(kVirtualKeyTab);
    single.active_pane = 0;
    single.pane_count = 1;
    EXPECT(resolve_key(single).target_pane == 0);

    // No pane at all is not a modulo by zero.
    auto empty = key(kVirtualKeyTab);
    empty.active_pane = 0;
    empty.pane_count = 0;
    EXPECT(resolve_key(empty).action == KeyAction::none);
}

void test_address_bar_focus_hides_every_key_from_the_shell() {
    for (const unsigned virtual_key :
         {kVirtualKeyTab, kVirtualKeyBack, unsigned{'V'}, unsigned{'X'}}) {
        auto input = key(virtual_key);
        input.address_bar_focused = true;
        EXPECT(resolve_key(input).shell == ShellAccelerator::never);
    }

    // Tab, Backspace and Ctrl+V belong to the address bar's own editing while
    // it has focus, so they resolve to no action at all.
    auto tab = key(kVirtualKeyTab);
    tab.address_bar_focused = true;
    EXPECT(resolve_key(tab).action == KeyAction::none);

    auto back = key(kVirtualKeyBack);
    back.address_bar_focused = true;
    EXPECT(resolve_key(back).action == KeyAction::none);

    auto paste = key('V');
    paste.control = true;
    paste.address_bar_focused = true;
    EXPECT(resolve_key(paste).action == KeyAction::none);

    // Tab commands still work from the address bar; they never reach the Shell.
    auto new_tab = key('T');
    new_tab.control = true;
    new_tab.address_bar_focused = true;
    EXPECT(resolve_key(new_tab).action == KeyAction::new_tab);
    EXPECT(resolve_key(new_tab).shell == ShellAccelerator::never);
}

void test_paste_acts_first_and_leaves_a_declined_key_to_the_shell() {
    auto paste = key('V');
    paste.control = true;
    const auto routing = resolve_key(paste);
    EXPECT(routing.action == KeyAction::paste);
    EXPECT(routing.shell == ShellAccelerator::after_action);

    // Ctrl+Shift+V is not our paste.
    paste.shift = true;
    EXPECT(resolve_key(paste).action == KeyAction::none);
}

void test_group_rename_needs_the_sidebar() {
    auto f2 = key(kVirtualKeyF2);
    EXPECT(resolve_key(f2).action == KeyAction::none);

    f2.sidebar_focused = true;
    const auto routing = resolve_key(f2);
    EXPECT(routing.action == KeyAction::begin_group_rename);
    EXPECT(routing.shell == ShellAccelerator::never);

    // Any modifier disqualifies it.
    f2.control = true;
    EXPECT(resolve_key(f2).action == KeyAction::none);
}

void test_tab_and_history_commands_yield_to_the_shell_first() {
    struct Case final {
        unsigned virtual_key;
        bool control;
        bool alt;
        bool shift;
        KeyAction expected;
    };
    const Case cases[]{
        {'T', true, false, false, KeyAction::new_tab},
        {'W', true, false, false, KeyAction::close_tab},
        {kVirtualKeyTab, true, false, false, KeyAction::next_tab},
        {kVirtualKeyTab, true, false, true, KeyAction::previous_tab},
        {kVirtualKeyLeft, false, true, false, KeyAction::history_back},
        {kVirtualKeyRight, false, true, false, KeyAction::history_forward},
        {kVirtualKeyBack, false, false, false, KeyAction::navigate_up},
    };
    for (const auto& item : cases) {
        auto input = key(item.virtual_key);
        input.control = item.control;
        input.alt = item.alt;
        input.shift = item.shift;
        const auto routing = resolve_key(input);
        EXPECT(routing.action == item.expected);
        EXPECT(routing.shell == ShellAccelerator::before_action);
    }
}

void test_f6_cycles_panes_but_after_the_shell() {
    const auto routing = resolve_key(key(kVirtualKeyF6));
    EXPECT(routing.action == KeyAction::focus_next_pane);
    EXPECT(routing.target_pane == 2);
    // Unlike plain Tab, a view that claims F6 keeps it.
    EXPECT(routing.shell == ShellAccelerator::before_action);

    // F6 is a plain key-down only; WM_SYSKEYDOWN is not it.
    EXPECT(resolve_key(key(kVirtualKeyF6, KeyMessage::system_key_down))
               .action == KeyAction::none);
}

void test_alt_chords_arrive_as_system_key_down() {
    auto back = key(kVirtualKeyLeft, KeyMessage::system_key_down);
    back.alt = true;
    EXPECT(resolve_key(back).action == KeyAction::history_back);

    // Ctrl+Alt is neither.
    back.control = true;
    EXPECT(resolve_key(back).action == KeyAction::none);
}

void test_non_key_messages_still_reach_the_shell() {
    const auto routing = resolve_key(key(0, KeyMessage::other));
    EXPECT(routing.action == KeyAction::none);
    EXPECT(routing.shell == ShellAccelerator::before_action);
}
}  // namespace

int main() {
    test_plain_tab_beats_the_shell_accelerator();
    test_pane_cycle_wraps_in_both_directions();
    test_address_bar_focus_hides_every_key_from_the_shell();
    test_paste_acts_first_and_leaves_a_declined_key_to_the_shell();
    test_group_rename_needs_the_sidebar();
    test_tab_and_history_commands_yield_to_the_shell_first();
    test_f6_cycles_panes_but_after_the_shell();
    test_alt_chords_arrive_as_system_key_down();
    test_non_key_messages_still_reach_the_shell();
    return panedock::test::summary("core_key_routing");
}
