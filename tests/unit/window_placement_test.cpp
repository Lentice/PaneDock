#include "app_shell/window_placement.h"

int main() {
    using panedock::app_shell::placement_is_offscreen;

    // A rect fully inside the virtual desktop is on-screen.
    static_assert(!placement_is_offscreen(100, 100, 1000, 700, 0, 0, 1920, 1040));
    static_assert(!placement_is_offscreen(0, 0, 1000, 700, 0, 0, 1920, 1040));

    // A rect fully outside the virtual desktop is off-screen.
    static_assert(placement_is_offscreen(100000, 100000, 1000, 700, 0, 0,
                                         1920, 1040));
    static_assert(placement_is_offscreen(-5000, 100, 1000, 700, 0, 0, 1920,
                                         1040));

    // A rect that only partially overlaps the desktop is treated as visible
    // (the user can drag it back) -- not off-screen.
    static_assert(!placement_is_offscreen(1820, 100, 1000, 700, 0, 0, 1920,
                                          1040));
    static_assert(!placement_is_offscreen(-500, 100, 1000, 700, 0, 0, 1920,
                                          1040));

    // Degenerate size is off-screen regardless of position (nothing to see).
    static_assert(placement_is_offscreen(100, 100, 0, 700, 0, 0, 1920, 1040));
    static_assert(placement_is_offscreen(100, 100, 1000, 0, 0, 0, 1920, 1040));

    // Virtual desktop starting at a negative origin (secondary monitor left of
    // a primary). A window on that secondary monitor must stay visible.
    static_assert(!placement_is_offscreen(-1800, 100, 900, 700, -1920, 0, 3840,
                                          1040));
    static_assert(placement_is_offscreen(10000, 100, 900, 700, -1920, 0, 3840,
                                         1040));

    // Default placement uses CW_USEDEFAULT (-1) for x/y: that is an on-screen
    // cascade slot, not off-screen.
    static_assert(!placement_is_offscreen(-1, -1, 1000, 700, 0, 0, 1920, 1040));

    return 0;
}
