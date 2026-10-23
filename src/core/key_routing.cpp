#include "core/key_routing.h"

namespace panedock::core {

namespace {

// The managed list's own keys, as in Explorer. They come before every other
// rule: the list has the key, so none of the pane-wide chords may claim it.
KeyAction resolve_list_key(const KeyInput& input) noexcept {
    if (input.message != KeyMessage::key_down || input.alt) return KeyAction::none;
    const unsigned key = input.virtual_key;
    if (!input.control) {
        if (key == kVirtualKeyDelete)
            return input.shift ? KeyAction::delete_permanently
                               : KeyAction::delete_items;
        if (input.shift) return KeyAction::none;
        if (key == kVirtualKeyF2) return KeyAction::begin_item_rename;
        if (key == kVirtualKeyReturn) return KeyAction::open_selection;
        return KeyAction::none;
    }
    if (input.shift) return key == 'N' ? KeyAction::new_folder : KeyAction::none;
    switch (key) {
    case 'A': return KeyAction::select_all;
    case 'C': return KeyAction::copy;
    case 'X': return KeyAction::cut;
    case 'V': return KeyAction::paste;
    default: return KeyAction::none;
    }
}

}  // namespace

KeyRouting resolve_key(const KeyInput& input) noexcept {
    const bool key_down = input.message == KeyMessage::key_down ||
                          input.message == KeyMessage::system_key_down;

    if (input.list_focused) {
        if (const KeyAction action = resolve_list_key(input);
            action != KeyAction::none)
            return {action, 0};
    }

    const auto step_pane = [&](bool reverse) noexcept -> KeyRouting {
        if (input.pane_count == 0) return {KeyAction::none, 0};
        const std::size_t next =
            reverse
                ? (input.active_pane + input.pane_count - 1) % input.pane_count
                : (input.active_pane + 1) % input.pane_count;
        return {reverse ? KeyAction::focus_previous_pane
                        : KeyAction::focus_next_pane,
                next};
    };

    // F2 renames the selected Group row, and only while the sidebar has focus.
    if (input.message == KeyMessage::key_down && !input.control &&
        !input.alt && !input.shift &&
        input.virtual_key == kVirtualKeyF2 && input.sidebar_focused)
        return {KeyAction::begin_group_rename, 0};

    // Ctrl+V pastes into the active pane's folder from outside the list too.
    if (key_down && input.control && !input.alt && !input.shift &&
        input.virtual_key == 'V' && !input.address_bar_focused)
        return {KeyAction::paste, 0};

    // Plain Tab is pane navigation, not the list's column header.
    if (key_down && !input.control && !input.alt &&
        input.virtual_key == kVirtualKeyTab && !input.address_bar_focused)
        return step_pane(input.shift);

    if (key_down && input.control && !input.alt && input.virtual_key == 'T')
        return {KeyAction::new_tab, 0};
    if (key_down && input.control && !input.alt && input.virtual_key == 'W')
        return {KeyAction::close_tab, 0};
    if (key_down && input.control && !input.alt &&
        input.virtual_key == kVirtualKeyTab)
        return {input.shift ? KeyAction::previous_tab : KeyAction::next_tab, 0};
    if (key_down && input.alt && !input.control &&
        input.virtual_key == kVirtualKeyLeft)
        return {KeyAction::history_back, 0};
    if (key_down && input.alt && !input.control &&
        input.virtual_key == kVirtualKeyRight)
        return {KeyAction::history_forward, 0};
    if (key_down && input.alt && !input.control &&
        input.virtual_key == kVirtualKeyUp)
        return {KeyAction::navigate_up, 0};
    // Windows 10/11 Explorer: Backspace is Back, not Up. In an edit it is
    // ordinary text editing.
    if (key_down && !input.control && !input.alt &&
        input.virtual_key == kVirtualKeyBack && !input.address_bar_focused)
        return {KeyAction::history_back, 0};

    // F5 and Ctrl+R refresh, from the list or the address bar, as in Explorer.
    if (input.message == KeyMessage::key_down && !input.control &&
        !input.alt && !input.shift && input.virtual_key == kVirtualKeyF5)
        return {KeyAction::refresh, 0};
    if (key_down && input.control && !input.alt && !input.shift &&
        input.virtual_key == 'R')
        return {KeyAction::refresh, 0};

    // Ctrl+L, Alt+D and F4 move focus to the address bar and select its text.
    // Alt+F4 is never ours: it stays the window close command.
    if ((key_down && input.control && !input.alt && !input.shift &&
         input.virtual_key == 'L') ||
        (key_down && input.alt && !input.control && !input.shift &&
         input.virtual_key == 'D') ||
        (input.message == KeyMessage::key_down && !input.control &&
         !input.alt && !input.shift && input.virtual_key == kVirtualKeyF4))
        return {KeyAction::focus_address_bar, 0};

    // F6 is the classic pane cycle.
    if (input.message == KeyMessage::key_down &&
        input.virtual_key == kVirtualKeyF6) {
        return step_pane(input.shift);
    }

    return {KeyAction::none, 0};
}

}  // namespace panedock::core
