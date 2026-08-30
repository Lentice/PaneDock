#pragma once

#include <algorithm>

namespace panedock::app_shell {

// PD-134: pure geometry for deciding whether a restored (non-maximized) window
// rect lies entirely outside the virtual desktop, which would leave the app
// open with no visible UI. Extracted so the off-screen detection is testable
// without a window. All values are screen-coordinate pixels.
constexpr bool placement_is_offscreen(int x, int y, int width, int height,
                                      int virtual_x, int virtual_y,
                                      int virtual_width,
                                      int virtual_height) noexcept {
    if (width <= 0 || height <= 0) return true;
    const int left = std::max(x, virtual_x);
    const int top = std::max(y, virtual_y);
    const int right = std::min(x + width, virtual_x + virtual_width);
    const int bottom = std::min(y + height, virtual_y + virtual_height);
    return left >= right || top >= bottom;
}

}  // namespace panedock::app_shell
