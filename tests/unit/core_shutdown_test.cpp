#include "core/shutdown.h"
#include "unit/test_util.h"

namespace {
using namespace panedock::core;

void queue_shutdown(ShutdownSequence& sequence) {
    EXPECT(sequence.step(ShutdownEvent::close_requested) ==
           ShutdownAction::defer);
    EXPECT(sequence.step(ShutdownEvent::deferred_shutdown_queued) ==
           ShutdownAction::none);
}

void finish_shutdown(ShutdownSequence& sequence) {
    EXPECT(sequence.step(ShutdownEvent::deferred_shutdown_ready) ==
           ShutdownAction::save_session);
    EXPECT(sequence.step(ShutdownEvent::save_succeeded) ==
           ShutdownAction::destroy_views);
    EXPECT(sequence.step(ShutdownEvent::teardown_started) ==
           ShutdownAction::destroy_views);
    EXPECT(sequence.step(ShutdownEvent::views_destroyed) ==
           ShutdownAction::destroy_window);
    EXPECT(sequence.step(ShutdownEvent::window_destroyed) ==
           ShutdownAction::write_clean_marker);
}

void test_normal_close_orders_actions() {
    ShutdownSequence sequence;
    queue_shutdown(sequence);
    finish_shutdown(sequence);
    EXPECT(sequence.state().closing_);
    EXPECT(sequence.state().quit_requested);
    EXPECT(sequence.state().main_window_destroyed);
    EXPECT(sequence.state().shutdown_clean_marker_armed);
}

void test_close_inside_nested_shell_calls_waits_for_outer_call() {
    ShutdownSequence sequence;
    EXPECT(sequence.step(ShutdownEvent::shell_call_entered) ==
           ShutdownAction::none);
    EXPECT(sequence.step(ShutdownEvent::shell_call_entered) ==
           ShutdownAction::none);
    EXPECT(sequence.step(ShutdownEvent::close_requested) ==
           ShutdownAction::defer);
    EXPECT(sequence.step(ShutdownEvent::shell_call_left) ==
           ShutdownAction::none);
    EXPECT(sequence.state().shell_call_depth == 1);
    EXPECT(sequence.step(ShutdownEvent::shell_call_left) ==
           ShutdownAction::defer);
    EXPECT(sequence.state().shell_call_depth == 0);
    EXPECT(sequence.step(ShutdownEvent::deferred_shutdown_queued) ==
           ShutdownAction::none);
    finish_shutdown(sequence);
}

void test_consumed_deferred_shutdown_message_is_reposted() {
    ShutdownSequence sequence;
    EXPECT(sequence.step(ShutdownEvent::close_requested) ==
           ShutdownAction::defer);
    EXPECT(sequence.step(ShutdownEvent::shell_call_entered) ==
           ShutdownAction::none);
    EXPECT(sequence.step(ShutdownEvent::shell_call_left) ==
           ShutdownAction::defer);
    EXPECT(sequence.step(ShutdownEvent::deferred_shutdown_queued) ==
           ShutdownAction::none);

    EXPECT(sequence.step(ShutdownEvent::shell_call_entered) ==
           ShutdownAction::none);
    EXPECT(sequence.step(ShutdownEvent::deferred_shutdown_ready) ==
           ShutdownAction::none);
    EXPECT(!sequence.state().shutdown_message_queued);
    EXPECT(sequence.step(ShutdownEvent::shell_call_left) ==
           ShutdownAction::defer);
    EXPECT(sequence.step(ShutdownEvent::shell_call_left) ==
           ShutdownAction::none);

    EXPECT(sequence.step(ShutdownEvent::deferred_shutdown_queued) ==
           ShutdownAction::none);
    EXPECT(sequence.step(ShutdownEvent::deferred_shutdown_ready) ==
           ShutdownAction::save_session);
}

void test_drag_defers_close_until_all_targets_finish() {
    ShutdownSequence close;
    EXPECT(close.step(ShutdownEvent::drag_started) == ShutdownAction::none);
    EXPECT(close.step(ShutdownEvent::close_requested) ==
           ShutdownAction::defer);
    EXPECT(close.state().drag_in_progress);
    EXPECT(close.state().shutdown_deferred);
    EXPECT(close.step(ShutdownEvent::deferred_shutdown_queued) ==
           ShutdownAction::none);
    EXPECT(close.step(ShutdownEvent::deferred_shutdown_ready) ==
           ShutdownAction::none);
    EXPECT(!close.state().shutdown_message_queued);
    EXPECT(close.state().drag_in_progress);

    EXPECT(close.step(ShutdownEvent::drag_started) == ShutdownAction::none);
    EXPECT(close.step(ShutdownEvent::drag_finished) == ShutdownAction::none);
    EXPECT(close.state().drag_in_progress);
    EXPECT(close.step(ShutdownEvent::drag_finished) == ShutdownAction::defer);
    EXPECT(!close.state().drag_in_progress);
    EXPECT(close.step(ShutdownEvent::deferred_shutdown_queued) ==
           ShutdownAction::none);
    EXPECT(close.step(ShutdownEvent::deferred_shutdown_ready) ==
           ShutdownAction::save_session);

    ShutdownSequence end_session;
    EXPECT(end_session.step(ShutdownEvent::drag_started) ==
           ShutdownAction::none);
    EXPECT(end_session.step(ShutdownEvent::end_session) ==
           ShutdownAction::defer);
    EXPECT(end_session.state().end_session_pending);
    EXPECT(end_session.state().shutdown_deferred);
    EXPECT(end_session.step(ShutdownEvent::deferred_shutdown_queued) ==
           ShutdownAction::none);
    EXPECT(end_session.step(ShutdownEvent::drag_finished) ==
           ShutdownAction::none);
    EXPECT(end_session.step(ShutdownEvent::deferred_shutdown_ready) ==
           ShutdownAction::save_session);
}

void test_transfer_decisions() {
    ShutdownSequence keep_open;
    keep_open.step(ShutdownEvent::file_operation_call_started);
    keep_open.step(ShutdownEvent::file_operation_started);
    EXPECT(keep_open.step(ShutdownEvent::close_requested) ==
           ShutdownAction::prompt_transfer);
    keep_open.step(ShutdownEvent::transfer_keep_open);
    EXPECT(!keep_open.state().close_after_file_operation);
    EXPECT(!keep_open.state().cancel_file_operation);

    ShutdownSequence after_transfer;
    after_transfer.step(ShutdownEvent::file_operation_call_started);
    after_transfer.step(ShutdownEvent::file_operation_started);
    after_transfer.step(ShutdownEvent::close_requested);
    after_transfer.step(ShutdownEvent::transfer_close_after_transfer);
    after_transfer.step(ShutdownEvent::file_operation_finished);
    after_transfer.step(ShutdownEvent::file_operation_call_finished);
    EXPECT(after_transfer.step(ShutdownEvent::close_requested) ==
           ShutdownAction::defer);
    EXPECT(after_transfer.state().close_after_file_operation);

    ShutdownSequence cancel;
    cancel.step(ShutdownEvent::file_operation_call_started);
    cancel.step(ShutdownEvent::file_operation_started);
    cancel.step(ShutdownEvent::close_requested);
    cancel.step(ShutdownEvent::transfer_cancel_and_close);
    EXPECT(cancel.state().close_after_file_operation);
    EXPECT(cancel.state().cancel_file_operation);
}

void test_end_session_paths_and_save_failure() {
    ShutdownSequence file_operation;
    file_operation.step(ShutdownEvent::file_operation_call_started);
    file_operation.step(ShutdownEvent::file_operation_started);
    EXPECT(file_operation.step(ShutdownEvent::end_session) ==
           ShutdownAction::none);
    EXPECT(file_operation.state().end_session_pending);
    EXPECT(file_operation.state().cancel_file_operation);
    file_operation.step(ShutdownEvent::file_operation_finished);
    file_operation.step(ShutdownEvent::file_operation_call_finished);
    EXPECT(file_operation.step(ShutdownEvent::close_requested) ==
           ShutdownAction::defer);

    ShutdownSequence save_failure;
    queue_shutdown(save_failure);
    EXPECT(save_failure.step(ShutdownEvent::deferred_shutdown_ready) ==
           ShutdownAction::save_session);
    EXPECT(save_failure.step(ShutdownEvent::save_failed) ==
           ShutdownAction::prompt_save_failure);
    save_failure.step(ShutdownEvent::save_prompt_started);
    EXPECT(save_failure.step(ShutdownEvent::end_session) ==
           ShutdownAction::none);
    EXPECT(save_failure.step(ShutdownEvent::save_prompt_finished) ==
           ShutdownAction::destroy_views);
    EXPECT(!save_failure.state().shutdown_clean_marker_armed);
}

void test_repeated_close_does_not_repeat_teardown() {
    ShutdownSequence sequence;
    queue_shutdown(sequence);
    EXPECT(sequence.step(ShutdownEvent::close_requested) ==
           ShutdownAction::none);
    finish_shutdown(sequence);
    EXPECT(sequence.step(ShutdownEvent::close_requested) ==
           ShutdownAction::none);
    EXPECT(sequence.step(ShutdownEvent::teardown_started) ==
           ShutdownAction::none);
}

// The single teardown gate. main.cpp asked this question 53 times by reading
// closing_ and shutdown_deferred by hand, in two operand orders; it is one
// predicate now, so the answer is stated once here.
void test_gate_covers_both_started_and_deferred_teardown() {
    ShutdownSequence idle;
    EXPECT(!idle.is_shutting_down());

    ShutdownSequence deferred;
    queue_shutdown(deferred);
    EXPECT(deferred.step(ShutdownEvent::close_requested) ==
           ShutdownAction::none);
    // Teardown is decided but waiting for the nested Shell call to unwind.
    EXPECT(!deferred.state().closing_);
    EXPECT(deferred.state().shutdown_deferred);
    EXPECT(deferred.is_shutting_down());

    ShutdownSequence closing;
    queue_shutdown(closing);
    finish_shutdown(closing);
    // Teardown has actually started.
    EXPECT(closing.state().closing_);
    EXPECT(closing.is_shutting_down());
}
}  // namespace

int main() {
    test_normal_close_orders_actions();
    test_close_inside_nested_shell_calls_waits_for_outer_call();
    test_consumed_deferred_shutdown_message_is_reposted();
    test_drag_defers_close_until_all_targets_finish();
    test_transfer_decisions();
    test_end_session_paths_and_save_failure();
    test_repeated_close_does_not_repeat_teardown();
    test_gate_covers_both_started_and_deferred_teardown();
    return panedock::test::summary("core_shutdown");
}
