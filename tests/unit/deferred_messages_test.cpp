#include "app_shell/deferred_messages.h"
#include "unit/test_util.h"

namespace {
using panedock::app_shell::DeferredMessage;
using panedock::app_shell::DeferredMessages;
using panedock::app_shell::deferred_message_replaces_previous;
using panedock::app_shell::hold_deferred_message;
using panedock::app_shell::take_deferred_messages;

HWND window(int id) noexcept { return reinterpret_cast<HWND>(id); }

DeferredMessage command(HWND target) { return {target, WM_COMMAND, 7, 0}; }

DeferredMessage click(HWND target, int x, int y,
                      UINT message = WM_LBUTTONDOWN) {
    return {target, message, 0, MAKELPARAM(x, y)};
}

bool hold(DeferredMessages& held, const DeferredMessage& message) {
    return hold_deferred_message(
        held, message, deferred_message_replaces_previous(message.message));
}

void test_a_repeated_command_is_held_once() {
    DeferredMessages held;
    EXPECT(hold(held, command(window(1))));
    EXPECT(!hold(held, command(window(1))));
    EXPECT(!hold(held, command(window(1))));
    EXPECT(held.size() == 1);
}

void test_the_target_is_part_of_the_identity() {
    DeferredMessages held;
    EXPECT(hold(held, command(window(1))));
    // Same message, different window: a pane command and a sidebar command
    // must both survive, and each must go back to its own HWND.
    EXPECT(hold(held, command(window(2))));
    EXPECT(held.size() == 2);
    EXPECT(held[0].target == window(1));
    EXPECT(held[1].target == window(2));
}

void test_taking_the_batch_empties_the_queue() {
    DeferredMessages held;
    (void)hold(held, command(window(1)));
    (void)hold(held, command(window(2)));
    const DeferredMessages taken = take_deferred_messages(held);
    EXPECT(taken.size() == 2);
    EXPECT(held.empty());
    // A message dispatched from the released batch can hold a new one, and it
    // must not be mistaken for a duplicate of one already sent.
    EXPECT(hold(held, command(window(1))));
    EXPECT(held.size() == 1);
    EXPECT(take_deferred_messages(held).size() == 1);
    EXPECT(take_deferred_messages(held).empty());
}

// PD-212: a click's payload is its cursor position, so N clicks at N positions
// used to become N entries -- and replaying them meant N slow navigations at
// coordinates the user had already left.
void test_clicks_at_different_positions_keep_only_the_last() {
    DeferredMessages held;
    EXPECT(hold(held, click(window(1), 10, 5)));
    EXPECT(!hold(held, click(window(1), 40, 5)));
    EXPECT(!hold(held, click(window(1), 90, 5)));
    EXPECT(held.size() == 1);
    EXPECT(held[0].lparam == MAKELPARAM(90, 5));
}

void test_replacement_is_per_target_and_per_message() {
    DeferredMessages held;
    EXPECT(hold(held, click(window(1), 10, 5)));
    // A different strip is a different intent, not a newer one.
    EXPECT(hold(held, click(window(2), 20, 5)));
    // So is button-up versus button-down on the same strip.
    EXPECT(hold(held, click(window(1), 30, 5, WM_LBUTTONUP)));
    EXPECT(held.size() == 3);
    EXPECT(!hold(held, click(window(1), 99, 9)));
    EXPECT(held.size() == 3);
    EXPECT(held[0].lparam == MAKELPARAM(99, 9));
    EXPECT(held[1].target == window(2));
    EXPECT(held[2].message == WM_LBUTTONUP);
}

void test_replacement_keeps_the_original_queue_position() {
    DeferredMessages held;
    (void)hold(held, click(window(1), 10, 5));
    (void)hold(held, command(window(1)));
    (void)hold(held, click(window(1), 70, 5));
    // The click arrived first, so it still replays first -- only its position
    // was updated.
    EXPECT(held.size() == 2);
    EXPECT(held[0].message == WM_LBUTTONDOWN);
    EXPECT(held[0].lparam == MAKELPARAM(70, 5));
    EXPECT(held[1].message == WM_COMMAND);
}

void test_only_pointer_messages_replace() {
    EXPECT(deferred_message_replaces_previous(WM_LBUTTONDOWN));
    EXPECT(deferred_message_replaces_previous(WM_LBUTTONDBLCLK));
    EXPECT(deferred_message_replaces_previous(WM_LBUTTONUP));
    EXPECT(deferred_message_replaces_previous(WM_CONTEXTMENU));
    // WM_COMMAND must stay on full-equality dedup: Group switching relies on
    // a repeated LBN_SELCHANGE collapsing to one entry.
    EXPECT(!deferred_message_replaces_previous(WM_COMMAND));
    EXPECT(!deferred_message_replaces_previous(WM_PARENTNOTIFY));
}

// The caller decides for its own WM_APP ids; the queue honors the flag rather
// than re-deriving it.
void test_the_caller_can_force_replacement_for_its_own_message() {
    DeferredMessages held;
    constexpr UINT hover = WM_APP + 51;
    EXPECT(hold_deferred_message(held, {window(1), hover, 0, 1}, true));
    EXPECT(!hold_deferred_message(held, {window(1), hover, 0, 2}, true));
    EXPECT(!hold_deferred_message(held, {window(1), hover, 0, 3}, true));
    EXPECT(held.size() == 1);
    EXPECT(held[0].lparam == 3);
}
}  // namespace

int main() {
    test_a_repeated_command_is_held_once();
    test_the_target_is_part_of_the_identity();
    test_taking_the_batch_empties_the_queue();
    test_clicks_at_different_positions_keep_only_the_last();
    test_replacement_is_per_target_and_per_message();
    test_replacement_keeps_the_original_queue_position();
    test_only_pointer_messages_replace();
    test_the_caller_can_force_replacement_for_its_own_message();
    return panedock::test::summary("deferred_messages");
}
