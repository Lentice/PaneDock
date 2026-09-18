#include "app_shell/deferred_messages.h"
#include "unit/test_util.h"

namespace {
using panedock::app_shell::DeferredMessage;
using panedock::app_shell::DeferredMessages;
using panedock::app_shell::hold_deferred_message;
using panedock::app_shell::take_deferred_messages;

HWND window(int id) noexcept { return reinterpret_cast<HWND>(id); }

DeferredMessage click(HWND target, UINT message = WM_LBUTTONDOWN) {
    return {target, message, 0, 0};
}

void test_a_repeated_interaction_is_held_once() {
    DeferredMessages held;
    EXPECT(hold_deferred_message(held, click(window(1))));
    EXPECT(!hold_deferred_message(held, click(window(1))));
    EXPECT(!hold_deferred_message(held, click(window(1))));
    EXPECT(held.size() == 1);
}

void test_the_target_is_part_of_the_identity() {
    DeferredMessages held;
    EXPECT(hold_deferred_message(held, click(window(1))));
    // Same message, different window: a pane click and a sidebar click must
    // both survive, and each must go back to its own HWND.
    EXPECT(hold_deferred_message(held, click(window(2))));
    EXPECT(hold_deferred_message(held, click(window(1), WM_LBUTTONUP)));
    EXPECT(held.size() == 3);
    EXPECT(held[0].target == window(1));
    EXPECT(held[1].target == window(2));
    EXPECT(held[2].message == WM_LBUTTONUP);
}

void test_taking_the_batch_empties_the_queue() {
    DeferredMessages held;
    (void)hold_deferred_message(held, click(window(1)));
    (void)hold_deferred_message(held, click(window(2)));
    const DeferredMessages taken = take_deferred_messages(held);
    EXPECT(taken.size() == 2);
    EXPECT(held.empty());
    // A message dispatched from the released batch can hold a new one, and it
    // must not be mistaken for a duplicate of one already sent.
    EXPECT(hold_deferred_message(held, click(window(1))));
    EXPECT(held.size() == 1);
    EXPECT(take_deferred_messages(held).size() == 1);
    EXPECT(take_deferred_messages(held).empty());
}
}  // namespace

int main() {
    test_a_repeated_interaction_is_held_once();
    test_the_target_is_part_of_the_identity();
    test_taking_the_batch_empties_the_queue();
    return panedock::test::summary("deferred_messages");
}
