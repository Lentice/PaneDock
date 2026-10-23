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
// navigation, not the list's column header.
void test_plain_tab_is_pane_navigation() {
    const auto forward = resolve_key(key(kVirtualKeyTab));
    EXPECT(forward.action == KeyAction::focus_next_pane);
    EXPECT(forward.target_pane == 2);

    auto shifted = key(kVirtualKeyTab);
    shifted.shift = true;
    const auto backward = resolve_key(shifted);
    EXPECT(backward.action == KeyAction::focus_previous_pane);
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

void test_address_bar_keeps_its_editing_keys() {
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

    // Tab commands still work from the address bar.
    auto new_tab = key('T');
    new_tab.control = true;
    new_tab.address_bar_focused = true;
    EXPECT(resolve_key(new_tab).action == KeyAction::new_tab);
}

void test_paste_needs_plain_ctrl_v() {
    auto paste = key('V');
    paste.control = true;
    const auto routing = resolve_key(paste);
    EXPECT(routing.action == KeyAction::paste);

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

    // Any modifier disqualifies it.
    f2.control = true;
    EXPECT(resolve_key(f2).action == KeyAction::none);
}

void test_tab_and_history_commands() {
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
        // Windows 10/11 Explorer: Backspace goes Back, Alt+Up goes Up.
        {kVirtualKeyBack, false, false, false, KeyAction::history_back},
        {kVirtualKeyUp, false, true, false, KeyAction::navigate_up},
        {kVirtualKeyF5, false, false, false, KeyAction::refresh},
        {'R', true, false, false, KeyAction::refresh},
    };
    for (const auto& item : cases) {
        auto input = key(item.virtual_key);
        input.control = item.control;
        input.alt = item.alt;
        input.shift = item.shift;
        const auto routing = resolve_key(input);
        EXPECT(routing.action == item.expected);
    }
}

void test_f6_cycles_panes() {
    const auto routing = resolve_key(key(kVirtualKeyF6));
    EXPECT(routing.action == KeyAction::focus_next_pane);
    EXPECT(routing.target_pane == 2);

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

void test_explorer_navigation_keys() {
    // Ctrl+L, Alt+D and F4 focus the address bar, from the list or the bar.
    struct Case final {
        unsigned virtual_key;
        bool control;
        bool alt;
        KeyMessage message;
    };
    for (const auto& item : {Case{'L', true, false, KeyMessage::key_down},
                             Case{'D', false, true, KeyMessage::system_key_down},
                             Case{kVirtualKeyF4, false, false, KeyMessage::key_down}}) {
        for (const bool in_bar : {false, true}) {
            auto input = key(item.virtual_key, item.message);
            input.control = item.control;
            input.alt = item.alt;
            input.address_bar_focused = in_bar;
            const auto routing = resolve_key(input);
            EXPECT(routing.action == KeyAction::focus_address_bar);
        }
    }
    // Alt+F4 stays the window's close command.
    auto alt_f4 = key(kVirtualKeyF4, KeyMessage::system_key_down);
    alt_f4.alt = true;
    EXPECT(resolve_key(alt_f4).action == KeyAction::none);
    // Plain D and L are type-ahead in the list.
    EXPECT(resolve_key(key('D')).action == KeyAction::none);
    EXPECT(resolve_key(key('L')).action == KeyAction::none);
    // Alt+Up arrives as WM_SYSKEYDOWN; plain Up moves the list focus.
    auto up = key(kVirtualKeyUp, KeyMessage::system_key_down);
    up.alt = true;
    EXPECT(resolve_key(up).action == KeyAction::navigate_up);
    EXPECT(resolve_key(key(kVirtualKeyUp)).action == KeyAction::none);
    // F5 refreshes from the address bar too.
    auto f5 = key(kVirtualKeyF5);
    f5.address_bar_focused = true;
    EXPECT(resolve_key(f5).action == KeyAction::refresh);
    // Shift+F5 / Ctrl+Shift+R are not refresh chords here.
    f5.shift = true;
    EXPECT(resolve_key(f5).action == KeyAction::none);
}

void test_non_key_messages_have_no_action() {
    const auto routing = resolve_key(key(0, KeyMessage::other));
    EXPECT(routing.action == KeyAction::none);
}
// The managed list's own keys win while the list has the key, and mean
// nothing anywhere else.
void test_list_keys_need_the_list() {
    struct Case final {
        unsigned virtual_key;
        bool control;
        bool shift;
        KeyAction expected;
    };
    const Case cases[]{
        {kVirtualKeyF2, false, false, KeyAction::begin_item_rename},
        {kVirtualKeyReturn, false, false, KeyAction::open_selection},
        {'A', true, false, KeyAction::select_all},
        {'N', true, true, KeyAction::new_folder},
        {'C', true, false, KeyAction::copy},
        {'X', true, false, KeyAction::cut},
        {'V', true, false, KeyAction::paste},
        {kVirtualKeyDelete, false, false, KeyAction::delete_items},
        {kVirtualKeyDelete, false, true, KeyAction::delete_permanently},
    };
    for (const auto& item : cases) {
        auto input = key(item.virtual_key);
        input.control = item.control;
        input.shift = item.shift;
        input.list_focused = true;
        EXPECT(resolve_key(input).action == item.expected);
        // Alt, or a key-up, is never a list key.
        auto alt = input;
        alt.alt = true;
        EXPECT(resolve_key(alt).action != item.expected);
        auto up = input;
        up.message = KeyMessage::other;
        EXPECT(resolve_key(up).action == KeyAction::none);
        // Outside the list only Ctrl+V keeps a meaning: paste.
        input.list_focused = false;
        EXPECT(resolve_key(input).action ==
               (item.expected == KeyAction::paste ? KeyAction::paste
                                                  : KeyAction::none));
    }

    // A modifier the list key does not name disqualifies it.
    auto f2 = key(kVirtualKeyF2);
    f2.list_focused = true;
    f2.shift = true;
    EXPECT(resolve_key(f2).action == KeyAction::none);
    auto enter = key(kVirtualKeyReturn);
    enter.list_focused = true;
    enter.control = true;
    EXPECT(resolve_key(enter).action == KeyAction::none);
    auto ctrl_n = key('N');
    ctrl_n.list_focused = true;
    ctrl_n.control = true;
    EXPECT(resolve_key(ctrl_n).action == KeyAction::none);
    auto ctrl_delete = key(kVirtualKeyDelete);
    ctrl_delete.list_focused = true;
    ctrl_delete.control = true;
    EXPECT(resolve_key(ctrl_delete).action == KeyAction::none);

    // Pane-wide chords keep working from the list.
    auto tab = key(kVirtualKeyTab);
    tab.list_focused = true;
    EXPECT(resolve_key(tab).action == KeyAction::focus_next_pane);
    auto back = key(kVirtualKeyBack);
    back.list_focused = true;
    EXPECT(resolve_key(back).action == KeyAction::history_back);
}
}  // namespace

int main() {
    test_plain_tab_is_pane_navigation();
    test_pane_cycle_wraps_in_both_directions();
    test_address_bar_keeps_its_editing_keys();
    test_paste_needs_plain_ctrl_v();
    test_group_rename_needs_the_sidebar();
    test_tab_and_history_commands();
    test_f6_cycles_panes();
    test_alt_chords_arrive_as_system_key_down();
    test_explorer_navigation_keys();
    test_non_key_messages_have_no_action();
    test_list_keys_need_the_list();
    return panedock::test::summary("core_key_routing");
}
