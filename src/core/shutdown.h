#pragma once

namespace panedock::core {

enum class ShutdownEvent {
    close_requested,
    end_session,
    end_session_cancelled,
    shell_call_entered,
    shell_call_left,
    deferred_shutdown_queued,
    deferred_shutdown_ready,
    deferred_shutdown_queue_failed,
    file_operation_call_started,
    file_operation_call_finished,
    file_operation_started,
    file_operation_finished,
    transfer_keep_open,
    transfer_close_after_transfer,
    transfer_cancel_and_close,
    save_started,
    save_succeeded,
    save_failed,
    save_prompt_started,
    save_prompt_finished,
    save_keep_open,
    save_discarded,
    teardown_started,
    views_destroyed,
    window_destroyed,
};

enum class ShutdownAction {
    none,
    defer,
    prompt_transfer,
    prompt_save_failure,
    save_session,
    destroy_views,
    destroy_window,
    write_clean_marker,
    request_quit,
};

class ShutdownSequence final {
public:
    struct State final {
        bool quit_requested{};
        bool closing_{};
        bool shutdown_prompt_active{};
        bool shutdown_save_attempted{};
        bool shutdown_clean_marker_armed{};
        bool main_window_destroyed{};
        bool end_session_pending{};
        unsigned shell_call_depth{};
        bool shutdown_deferred{};
        bool shutdown_message_queued{};
        bool file_operation_call_active{};
        bool file_operation_in_progress{};
        bool close_after_file_operation{};
        bool cancel_file_operation{};
    };

    ShutdownAction step(ShutdownEvent event) noexcept;

    State& state() noexcept { return state_; }
    const State& state() const noexcept { return state_; }

private:
    State state_;
};

}  // namespace panedock::core
