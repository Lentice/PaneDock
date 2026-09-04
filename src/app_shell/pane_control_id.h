#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace panedock::app_shell {

enum class PaneControl {
    tab_strip,
    back,
    forward,
    up,
    address_bar,
    refresh,
    view_mode,
    pinned,
    folder_context,
};

// PD-189: the id encodes only the kind of control. Every pane's proc knows
// which pane it is, so the pane index no longer needs a place in the id.
inline constexpr std::array<std::pair<PaneControl, int>, 9>
    kPaneControlIdBases{{
        {PaneControl::tab_strip, 200},
        {PaneControl::back, 300},
        {PaneControl::forward, 310},
        {PaneControl::up, 320},
        {PaneControl::address_bar, 330},
        {PaneControl::refresh, 340},
        {PaneControl::view_mode, 350},
        {PaneControl::pinned, 392},
        {PaneControl::folder_context, 790},
    }};

inline std::optional<PaneControl> decode_pane_control(int id) noexcept {
    for (const auto& [control, base] : kPaneControlIdBases)
        if (id == base) return control;
    return std::nullopt;
}

inline int encode_pane_control(PaneControl control) noexcept {
    for (const auto& [candidate, base] : kPaneControlIdBases)
        if (candidate == control) return base;
    return -1;
}

}  // namespace panedock::app_shell
