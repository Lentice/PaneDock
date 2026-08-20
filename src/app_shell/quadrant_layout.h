#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>

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

}  // namespace panedock::app_shell
