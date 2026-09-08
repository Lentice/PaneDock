#include "core/key_routing.h"

namespace panedock::core {

KeyRouting resolve_key(const KeyInput& input) noexcept {
    const bool key_down = input.message == KeyMessage::key_down ||
                          input.message == KeyMessage::system_key_down;
    // Every key PaneDock does not claim outright is offered to the Shell view
    // first -- unless the address bar has focus, where the Shell view must not
    // see the keystroke at all.
    const ShellAccelerator shell_default =
        input.address_bar_focused ? ShellAccelerator::never
                                  : ShellAccelerator::before_action;

    const auto step_pane = [&](bool reverse) noexcept -> KeyRouting {
        if (input.pane_count == 0)
            return {KeyAction::none, shell_default, 0};
        const std::size_t next =
            reverse
                ? (input.active_pane + input.pane_count - 1) % input.pane_count
                : (input.active_pane + 1) % input.pane_count;
        return {reverse ? KeyAction::focus_previous_pane
                        : KeyAction::focus_next_pane,
                ShellAccelerator::never, next};
    };

    // F2 renames the selected Group row, and only while the sidebar has focus.
    if (input.message == KeyMessage::key_down && !input.control &&
        !input.alt && !input.shift &&
        input.virtual_key == kVirtualKeyF2 && input.sidebar_focused)
        return {KeyAction::begin_group_rename, ShellAccelerator::never, 0};

    // Ctrl+V pastes into the active pane's folder. When there is nothing
    // pastable the Shell view still gets the key.
    if (key_down && input.control && !input.alt && !input.shift &&
        input.virtual_key == 'V' && !input.address_bar_focused)
        return {KeyAction::paste, ShellAccelerator::after_action, 0};

    // Pane navigation owns plain Tab before the Shell can focus its header.
    if (key_down && !input.control && !input.alt &&
        input.virtual_key == kVirtualKeyTab && !input.address_bar_focused)
        return step_pane(input.shift);

    if (key_down && input.control && !input.alt && input.virtual_key == 'T')
        return {KeyAction::new_tab, shell_default, 0};
    if (key_down && input.control && !input.alt && input.virtual_key == 'W')
        return {KeyAction::close_tab, shell_default, 0};
    if (key_down && input.control && !input.alt &&
        input.virtual_key == kVirtualKeyTab)
        return {input.shift ? KeyAction::previous_tab : KeyAction::next_tab,
                shell_default, 0};
    if (key_down && input.alt && !input.control &&
        input.virtual_key == kVirtualKeyLeft)
        return {KeyAction::history_back, shell_default, 0};
    if (key_down && input.alt && !input.control &&
        input.virtual_key == kVirtualKeyRight)
        return {KeyAction::history_forward, shell_default, 0};
    if (key_down && !input.control && !input.alt &&
        input.virtual_key == kVirtualKeyBack && !input.address_bar_focused)
        return {KeyAction::navigate_up, shell_default, 0};

    // F6 is the classic pane cycle. Unlike plain Tab it sits after the Shell
    // accelerator, so a view that claims F6 keeps it.
    if (input.message == KeyMessage::key_down &&
        input.virtual_key == kVirtualKeyF6) {
        KeyRouting routing = step_pane(input.shift);
        routing.shell = shell_default;
        return routing;
    }

    return {KeyAction::none, shell_default, 0};
}

}  // namespace panedock::core
