#include "core/shutdown.h"

namespace panedock::core {

namespace {

// A deferred shutdown may only resume once every gate that can hold it back
// has cleared. shell_call_left, drag_finished and deferred_shutdown_ready
// each observe only one gate clearing; checking the full set here (rather
// than duplicating a partial check at each call site) is what keeps the
// three re-arm paths from racing each other into an early defer.
bool shutdown_ready_to_resume(const ShutdownSequence::State& state) noexcept {
    return state.shutdown_deferred && !state.shutdown_message_queued &&
           !state.closing_ && state.shell_call_depth == 0 &&
           state.drag_target_count == 0;
}

}  // namespace

ShutdownAction ShutdownSequence::step(ShutdownEvent event) noexcept {
    switch (event) {
        case ShutdownEvent::close_requested:
            if (state_.closing_ || state_.quit_requested ||
                state_.shutdown_deferred || state_.shutdown_message_queued ||
                state_.shutdown_prompt_active ||
                state_.shutdown_save_attempted)
                return ShutdownAction::none;
            if (state_.file_operation_in_progress) {
                state_.close_after_file_operation = true;
                return ShutdownAction::prompt_transfer;
            }
            state_.shutdown_deferred = true;
            return ShutdownAction::defer;

        case ShutdownEvent::end_session:
            state_.end_session_pending = true;
            if (state_.closing_ || state_.quit_requested) return ShutdownAction::none;
            if (state_.file_operation_call_active ||
                state_.file_operation_in_progress) {
                state_.close_after_file_operation = true;
                state_.cancel_file_operation = true;
                return ShutdownAction::none;
            }
            if (state_.drag_in_progress) {
                state_.shutdown_deferred = true;
                return ShutdownAction::defer;
            }
            if (state_.shutdown_prompt_active ||
                state_.shutdown_save_attempted)
                return ShutdownAction::none;
            if (state_.shutdown_deferred || state_.shutdown_message_queued)
                return ShutdownAction::none;
            state_.shutdown_deferred = true;
            return ShutdownAction::defer;

        case ShutdownEvent::end_session_cancelled:
            if (!state_.closing_) state_.end_session_pending = false;
            return ShutdownAction::none;

        case ShutdownEvent::drag_started:
            ++state_.drag_target_count;
            state_.drag_in_progress = true;
            return ShutdownAction::none;

        case ShutdownEvent::drag_finished:
            if (state_.drag_target_count == 0) return ShutdownAction::none;
            --state_.drag_target_count;
            state_.drag_in_progress = state_.drag_target_count != 0;
            return shutdown_ready_to_resume(state_) ? ShutdownAction::defer
                                                     : ShutdownAction::none;

        case ShutdownEvent::shell_call_entered:
            ++state_.shell_call_depth;
            return ShutdownAction::none;

        case ShutdownEvent::shell_call_left:
            if (state_.shell_call_depth == 0) return ShutdownAction::none;
            --state_.shell_call_depth;
            return shutdown_ready_to_resume(state_) ? ShutdownAction::defer
                                                     : ShutdownAction::none;

        case ShutdownEvent::deferred_shutdown_queued:
            if (state_.shutdown_deferred) state_.shutdown_message_queued = true;
            return ShutdownAction::none;

        case ShutdownEvent::deferred_shutdown_ready:
            if (state_.closing_ || !state_.shutdown_deferred)
                return ShutdownAction::none;
            if (state_.shell_call_depth != 0 || state_.drag_in_progress) {
                // A nested Shell pump or an active OLE drag may consume the
                // posted continuation; shell_call_left / drag_finished will
                // re-arm it once every gate in shutdown_ready_to_resume
                // clears.
                state_.shutdown_message_queued = false;
                return ShutdownAction::none;
            }
            state_.shutdown_deferred = false;
            state_.shutdown_message_queued = false;
            if (state_.shutdown_prompt_active ||
                state_.shutdown_save_attempted)
                return ShutdownAction::none;
            state_.shutdown_save_attempted = true;
            return ShutdownAction::save_session;

        case ShutdownEvent::deferred_shutdown_queue_failed:
            state_.shutdown_deferred = false;
            state_.shutdown_message_queued = false;
            return ShutdownAction::none;

        case ShutdownEvent::file_operation_call_started:
            state_.file_operation_call_active = true;
            state_.file_operation_in_progress = false;
            state_.close_after_file_operation = false;
            state_.cancel_file_operation = false;
            return ShutdownAction::none;

        case ShutdownEvent::file_operation_call_finished:
            state_.file_operation_call_active = false;
            return ShutdownAction::none;

        case ShutdownEvent::file_operation_started:
            state_.file_operation_in_progress = true;
            return ShutdownAction::none;

        case ShutdownEvent::file_operation_finished:
            state_.file_operation_in_progress = false;
            return ShutdownAction::none;

        case ShutdownEvent::transfer_keep_open:
            state_.close_after_file_operation = false;
            state_.cancel_file_operation = false;
            return ShutdownAction::none;

        case ShutdownEvent::transfer_close_after_transfer:
            state_.close_after_file_operation = true;
            state_.cancel_file_operation = false;
            return ShutdownAction::none;

        case ShutdownEvent::transfer_cancel_and_close:
            state_.close_after_file_operation = true;
            state_.cancel_file_operation = true;
            return ShutdownAction::none;

        case ShutdownEvent::save_started:
            state_.shutdown_save_attempted = true;
            return ShutdownAction::none;

        case ShutdownEvent::save_succeeded:
            state_.shutdown_clean_marker_armed = true;
            return ShutdownAction::destroy_views;

        case ShutdownEvent::save_failed:
            state_.shutdown_clean_marker_armed = false;
            return state_.end_session_pending
                       ? ShutdownAction::destroy_views
                       : ShutdownAction::prompt_save_failure;

        case ShutdownEvent::save_prompt_started:
            state_.shutdown_prompt_active = true;
            return ShutdownAction::none;

        case ShutdownEvent::save_prompt_finished:
            state_.shutdown_prompt_active = false;
            return state_.end_session_pending ? ShutdownAction::destroy_views
                                              : ShutdownAction::none;

        case ShutdownEvent::save_keep_open:
            state_.shutdown_prompt_active = false;
            state_.shutdown_save_attempted = false;
            state_.shutdown_clean_marker_armed = false;
            return ShutdownAction::none;

        case ShutdownEvent::save_discarded:
            state_.shutdown_prompt_active = false;
            return ShutdownAction::destroy_views;

        case ShutdownEvent::teardown_started:
            if (state_.closing_) return ShutdownAction::none;
            state_.closing_ = true;
            state_.quit_requested = true;
            state_.end_session_pending = false;
            state_.shutdown_deferred = false;
            state_.shutdown_message_queued = false;
            return ShutdownAction::destroy_views;

        case ShutdownEvent::views_destroyed:
            return state_.closing_ ? ShutdownAction::destroy_window
                                   : ShutdownAction::none;

        case ShutdownEvent::window_destroyed:
            state_.main_window_destroyed = true;
            state_.quit_requested = true;
            return state_.shutdown_clean_marker_armed
                       ? ShutdownAction::write_clean_marker
                       : ShutdownAction::request_quit;
    }
    return ShutdownAction::none;
}

}  // namespace panedock::core
