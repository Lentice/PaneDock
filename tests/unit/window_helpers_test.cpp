// The teardown allowlist the three child window procs share.
//
// This used to be spelled out by hand in each proc. The predicate is pure, so
// the rule can be stated here without a window: repainting and unhooking must
// still get through, everything else must not.

#include "app_shell/window_helpers.h"
#include "unit/test_util.h"

namespace {

using panedock::app_shell::child_message_blocked_while_closing;

void test_nothing_is_blocked_before_teardown() {
    EXPECT(!child_message_blocked_while_closing(false, WM_COMMAND));
    EXPECT(!child_message_blocked_while_closing(false, WM_LBUTTONDOWN));
    EXPECT(!child_message_blocked_while_closing(false, WM_PAINT));
}

void test_teardown_blocks_ordinary_messages() {
    EXPECT(child_message_blocked_while_closing(true, WM_COMMAND));
    EXPECT(child_message_blocked_while_closing(true, WM_LBUTTONDOWN));
    EXPECT(child_message_blocked_while_closing(true, WM_MOUSEMOVE));
    EXPECT(child_message_blocked_while_closing(true, WM_DRAWITEM));
}

// "Closing..." has to stay on screen through the synchronous Shell teardown,
// and a subclassed control has to see WM_NCDESTROY to unhook itself.
void test_teardown_still_allows_paint_and_ncdestroy() {
    EXPECT(!child_message_blocked_while_closing(true, WM_PAINT));
    EXPECT(!child_message_blocked_while_closing(true, WM_ERASEBKGND));
    EXPECT(!child_message_blocked_while_closing(true, WM_NCDESTROY));
}

}  // namespace

int main() {
    test_nothing_is_blocked_before_teardown();
    test_teardown_blocks_ordinary_messages();
    test_teardown_still_allows_paint_and_ncdestroy();
    return panedock::test::summary("window_helpers_test");
}
