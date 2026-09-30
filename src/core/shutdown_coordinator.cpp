#include "core/shutdown_coordinator.h"

namespace panedock::core {

void ShutdownCoordinator::request_close() noexcept {
    begin(!sequence_.state().end_session_pending);
}

void ShutdownCoordinator::begin(bool allow_keep_open) noexcept {
    run(sequence_.step(allow_keep_open ? ShutdownEvent::close_requested
                                       : ShutdownEvent::end_session));
}

void ShutdownCoordinator::deferred_shutdown_ready() noexcept {
    run(sequence_.step(ShutdownEvent::deferred_shutdown_ready));
}

void ShutdownCoordinator::drag_changed(bool entering) noexcept {
    const auto action = sequence_.step(entering ? ShutdownEvent::drag_started
                                                : ShutdownEvent::drag_finished);
    if (action == ShutdownAction::defer) run(action);
}

void ShutdownCoordinator::complete_deferred_close() noexcept {
    const auto &state = sequence_.state();
    if (!state.close_after_file_operation || state.file_operation_call_active ||
        state.file_operation_in_progress || state.closing_ ||
        !effects_.has_window())
        return;
    effects_.dismiss_transfer_prompt();
    request_close();
}

void ShutdownCoordinator::transfer_chosen(TransferChoice choice) noexcept {
    switch (choice) {
        case TransferChoice::keep_open:
            sequence_.step(ShutdownEvent::transfer_keep_open);
            return;
        case TransferChoice::close_after_transfer:
            sequence_.step(ShutdownEvent::transfer_close_after_transfer);
            complete_deferred_close();
            return;
        case TransferChoice::cancel_and_close:
            sequence_.step(ShutdownEvent::transfer_cancel_and_close);
            return;
    }
}

void ShutdownCoordinator::end_session_confirmed() noexcept {
    const auto action = sequence_.step(ShutdownEvent::end_session);

    // The durable checkpoint comes first, unconditionally: nothing may come
    // between here and the write -- not the gates below, and not closing_. A
    // normal close already stuck in Shell teardown when WM_ENDSESSION arrives
    // would otherwise be killed with the marker still false (PD-203).
    const bool saved = effects_.write_end_session_checkpoint();

    // Teardown only. A nested Shell pump or a live OLE drag still owns the
    // stack, and tearing views down under it is the crash the spec warns
    // about; an already-running close owns the sequence itself. The session
    // is durable either way, so skipping this costs a leak in a process the
    // OS is about to end.
    const auto &state = sequence_.state();
    if (state.closing_) return;
    if (action != ShutdownAction::defer || state.shell_call_depth != 0 ||
        state.drag_in_progress)
        return;
    // Only mark a save attempt if teardown will actually proceed. A cancelled
    // OS session end can otherwise strand a Shell-gated close.
    sequence_.step(ShutdownEvent::save_started);
    run(sequence_.step(saved ? ShutdownEvent::save_succeeded
                             : ShutdownEvent::save_failed));
}

void ShutdownCoordinator::end_session_cancelled() noexcept {
    const bool was_pending = sequence_.state().end_session_pending;
    sequence_.step(ShutdownEvent::end_session_cancelled);
    if (was_pending && !sequence_.is_shutting_down())
        effects_.show_closing_caption(false);
}

// The teardown order is the rule "never destroy the parent HWND while a view
// is alive": views first, the window only once the reducer has seen them go.
void ShutdownCoordinator::finish() noexcept {
    // teardown_started clears end_session_pending, so read it first.
    const bool ending = sequence_.state().end_session_pending;
    if (sequence_.step(ShutdownEvent::teardown_started) !=
        ShutdownAction::destroy_views)
        return;
    effects_.destroy_views(ending);
    if (sequence_.step(ShutdownEvent::views_destroyed) ==
        ShutdownAction::destroy_window)
        effects_.destroy_window();
}

void ShutdownCoordinator::run(ShutdownAction action) noexcept {
    switch (action) {
        case ShutdownAction::defer: {
            // The caption goes up before yielding to the message loop, so a
            // watching user sees the close was taken.
            effects_.show_closing_caption(true);
            const auto &state = sequence_.state();
            // A nested Shell call re-arms the continuation when it unwinds
            // (ShellReentryGuard); a queued one is already on its way.
            if (state.shell_call_depth != 0 || state.shutdown_message_queued ||
                !effects_.has_window())
                return;
            sequence_.step(ShutdownEvent::deferred_shutdown_queued);
            if (effects_.post_deferred_shutdown()) return;
            sequence_.step(ShutdownEvent::deferred_shutdown_queue_failed);
            if (effects_.window_alive()) request_close();
            return;
        }

        case ShutdownAction::prompt_transfer:
            effects_.show_transfer_prompt();
            return;

        case ShutdownAction::save_session: {
            const bool saved = effects_.save_session();
            run(sequence_.step(saved ? ShutdownEvent::save_succeeded
                                     : ShutdownEvent::save_failed));
            return;
        }

        case ShutdownAction::prompt_save_failure: {
            sequence_.step(ShutdownEvent::save_prompt_started);
            const bool keep_open = effects_.ask_keep_open_after_save_failure();
            // A confirmed session end that arrived inside the modal prompt
            // wins over whatever the user answered.
            if (sequence_.step(ShutdownEvent::save_prompt_finished) ==
                ShutdownAction::destroy_views) {
                finish();
                return;
            }
            // Only an explicit No closes after an interactive failure.
            if (keep_open) {
                sequence_.step(ShutdownEvent::save_keep_open);
                effects_.show_closing_caption(false);
                return;
            }
            run(sequence_.step(ShutdownEvent::save_discarded));
            return;
        }

        case ShutdownAction::destroy_views:
            finish();
            return;

        default:
            return;
    }
}

}  // namespace panedock::core
