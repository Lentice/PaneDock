#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace panedock::app_shell {

inline constexpr std::size_t kPaneControlCount = 4;

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

struct PaneControlId final {
    std::size_t pane;
    PaneControl control;
    bool operator==(const PaneControlId&) const = default;
};

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

inline std::optional<PaneControlId> decode_pane_control(int id) noexcept {
    for (const auto& [control, base] : kPaneControlIdBases) {
        const int pane = id - base;
        if (pane >= 0 && pane < static_cast<int>(kPaneControlCount))
            return PaneControlId{static_cast<std::size_t>(pane), control};
    }
    return std::nullopt;
}

inline int encode_pane_control(PaneControl control,
                               std::size_t pane) noexcept {
    for (const auto& [candidate, base] : kPaneControlIdBases)
        if (candidate == control) return base + static_cast<int>(pane);
    return -1;
}

}  // namespace panedock::app_shell
