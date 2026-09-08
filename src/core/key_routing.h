#pragma once

#include <cstddef>

namespace panedock::core {

// Virtual-key codes the routing rules name. core takes no Windows header,
// so they are restated here rather than included; main.cpp static_asserts each
// one against the Win32 VK_* value, which keeps the two from drifting.
// Letter keys need no constant: a Win32 virtual-key code for A-Z is its
// uppercase ASCII value.
inline constexpr unsigned kVirtualKeyBack = 0x08;
inline constexpr unsigned kVirtualKeyTab = 0x09;
inline constexpr unsigned kVirtualKeyLeft = 0x25;
inline constexpr unsigned kVirtualKeyRight = 0x27;
inline constexpr unsigned kVirtualKeyF2 = 0x71;
inline constexpr unsigned kVirtualKeyF6 = 0x75;

enum class KeyMessage {
    other,           // Anything that is not a key-down.
    key_down,        // WM_KEYDOWN
    system_key_down, // WM_SYSKEYDOWN (an Alt chord)
};

enum class KeyAction {
    none,
    begin_group_rename,
    paste,
    focus_next_pane,
    focus_previous_pane,
    new_tab,
    close_tab,
    next_tab,
    previous_tab,
    history_back,
    history_forward,
    navigate_up,
};

// Where the Shell view's own accelerator sits relative to `action`. This is
// the precedence the old fall-through chain encoded in source order alone,
// and it is the part that has actually regressed before: plain Tab must reach
// pane navigation before the Shell view can focus its column header.
enum class ShellAccelerator {
    never,          // PaneDock owns the key outright.
    before_action,  // The Shell gets first refusal; act only if it declines.
    after_action,   // PaneDock acts first; the Shell gets what the action declines.
};

struct KeyInput final {
    KeyMessage message{KeyMessage::other};
    unsigned virtual_key{};
    bool control{};
    bool alt{};
    bool shift{};
    bool address_bar_focused{};
    bool sidebar_focused{};
    std::size_t active_pane{};
    std::size_t pane_count{};
};

struct KeyRouting final {
    KeyAction action{KeyAction::none};
    ShellAccelerator shell{ShellAccelerator::never};
    // Only meaningful for focus_next_pane / focus_previous_pane.
    std::size_t target_pane{};

    bool operator==(const KeyRouting&) const = default;
};

KeyRouting resolve_key(const KeyInput& input) noexcept;

}  // namespace panedock::core
