#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>

#include "app_shell/layout_state.h"

namespace panedock::app_shell {

inline std::array<RECT, 4> quadrant_rects(const RECT& client) noexcept {
    const LONG width = client.right > client.left
                           ? client.right - client.left
                           : 0;
    const LONG height = client.bottom > client.top
                            ? client.bottom - client.top
                            : 0;
    const LONG right = client.left + width;
    const LONG bottom = client.top + height;
    const LONG middle_x = client.left + width / 2;
    const LONG middle_y = client.top + height / 2;

    return {{{client.left, client.top, middle_x, middle_y},
             {middle_x, client.top, right, middle_y},
             {client.left, middle_y, middle_x, bottom},
             {middle_x, middle_y, right, bottom}}};
}

inline std::array<RECT, 4> two_pane_rects(const RECT& client) noexcept {
    const LONG width = client.right > client.left
                           ? client.right - client.left
                           : 0;
    const LONG height = client.bottom > client.top
                            ? client.bottom - client.top
                            : 0;
    const LONG right = client.left + width;
    const LONG middle_x = client.left + width / 2;

    return {{{client.left, client.top, middle_x, client.top + height},
             {middle_x, client.top, right, client.top + height},
             {client.left, client.top, client.left, client.top},
             {client.left, client.top, client.left, client.top}}};
}

inline std::array<RECT, 4> layout_rects(
    const RECT& client, LayoutTemplate layout) noexcept {
    return layout == LayoutTemplate::four_pane ? quadrant_rects(client)
                                                : two_pane_rects(client);
}

}  // namespace panedock::app_shell
