#include "app_shell/tab_overflow.h"

int main() {
    constexpr auto fits =
        panedock::app_shell::tab_strip_viewport(300, 500, 28);
    static_assert(!fits.overflow);
    static_assert(fits.width == 500);
    static_assert(fits.scroll_button_width == 0);

    constexpr auto overflows =
        panedock::app_shell::tab_strip_viewport(800, 500, 28);
    static_assert(overflows.overflow);
    static_assert(overflows.width == 444);
    static_assert(overflows.scroll_button_width == 28);
    static_assert(panedock::app_shell::clamp_tab_scroll_offset(-1, 800, 444) ==
                  0);
    static_assert(panedock::app_shell::clamp_tab_scroll_offset(123, 800, 444) ==
                  123);
    static_assert(panedock::app_shell::clamp_tab_scroll_offset(999, 800, 444) ==
                  356);
    return 0;
}
