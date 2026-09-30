#include "core/shutdown_coordinator.h"
#include "unit/test_util.h"

#include <string>
#include <vector>

// ShutdownCoordinator's output is the sequence of effects it runs; the fake
// records them. The order used to be pinned by regex over main.cpp
// (shutdown_state_check.ps1), which a reordering that kept the strings passed.

namespace {
using namespace panedock::core;

class RecordingEffects final : public ShutdownEffects {
  public:
    std::vector<std::string> log;
    bool window{true};
    bool alive{true};
    bool post_succeeds{true};
    // Posts that fail before post_succeeds takes over.
    int failing_posts{};
    bool save_succeeds{true};
    bool checkpoint_succeeds{true};
    bool keep_open{true};
    // Runs inside the modal save-failure prompt, as a nested message would.
    ShutdownCoordinator *nested_end_session{nullptr};

    void show_closing_caption(bool closing) noexcept override {
        log.push_back(closing ? "caption:closing" : "caption:normal");
    }
    bool has_window() const noexcept override { return window; }
    bool window_alive() const noexcept override { return alive; }
    bool post_deferred_shutdown() noexcept override {
        log.push_back("post");
        if (failing_posts > 0) {
            --failing_posts;
            return false;
        }
        return post_succeeds;
    }
    void show_transfer_prompt() noexcept override {
        log.push_back("transfer_prompt");
    }
    void dismiss_transfer_prompt() noexcept override {
        log.push_back("dismiss_transfer_prompt");
    }
    bool save_session() noexcept override {
        log.push_back("save");
        return save_succeeds;
    }
    bool write_end_session_checkpoint() noexcept override {
        log.push_back("checkpoint");
        return checkpoint_succeeds;
    }
    bool ask_keep_open_after_save_failure() noexcept override {
        log.push_back("save_failure_prompt");
        if (nested_end_session != nullptr)
            (void)nested_end_session->sequence().step(
                ShutdownEvent::end_session);
        return keep_open;
    }
    void destroy_views(bool session_ending) noexcept override {
        log.push_back(session_ending ? "destroy_views:ending"
                                     : "destroy_views");
    }
    void destroy_window() noexcept override { log.push_back("destroy_window"); }
};

using Log = std::vector<std::string>;

void a_close_posts_then_saves_then_destroys_views_before_the_window() {
    RecordingEffects effects;
    ShutdownCoordinator coordinator(effects);
    coordinator.request_close();
    EXPECT((effects.log == Log{"caption:closing", "post"}));
    EXPECT(coordinator.is_shutting_down());

    effects.log.clear();
    coordinator.deferred_shutdown_ready();
    EXPECT((effects.log == Log{"save", "destroy_views", "destroy_window"}));
    EXPECT(coordinator.state().closing_);
    EXPECT(coordinator.state().shutdown_clean_marker_armed);
}

void a_close_inside_a_shell_call_waits_without_posting() {
    RecordingEffects effects;
    ShutdownCoordinator coordinator(effects);
    coordinator.sequence().step(ShutdownEvent::shell_call_entered);
    coordinator.request_close();
    // ShellReentryGuard posts it when the call unwinds.
    EXPECT((effects.log == Log{"caption:closing"}));
    EXPECT(coordinator.is_shutting_down());
}

void a_failed_post_retries_the_close_at_once() {
    RecordingEffects effects;
    effects.failing_posts = 1;
    ShutdownCoordinator coordinator(effects);
    coordinator.request_close();
    // The close is not dropped: it starts again and this time the post lands.
    EXPECT((effects.log ==
            Log{"caption:closing", "post", "caption:closing", "post"}));
    EXPECT(coordinator.state().shutdown_message_queued);
}

void a_failed_post_without_a_live_window_stops() {
    RecordingEffects effects;
    effects.post_succeeds = false;
    effects.alive = false;
    ShutdownCoordinator coordinator(effects);
    coordinator.request_close();
    EXPECT((effects.log == Log{"caption:closing", "post"}));
    EXPECT(!coordinator.is_shutting_down());
}

void a_failed_save_asks_and_keep_open_restores_the_caption() {
    RecordingEffects effects;
    effects.save_succeeds = false;
    effects.keep_open = true;
    ShutdownCoordinator coordinator(effects);
    coordinator.request_close();
    effects.log.clear();
    coordinator.deferred_shutdown_ready();
    EXPECT((effects.log ==
            Log{"save", "save_failure_prompt", "caption:normal"}));
    EXPECT(!coordinator.is_shutting_down());
    EXPECT(!coordinator.state().shutdown_save_attempted);
}

void a_failed_save_closes_without_the_marker_on_an_explicit_no() {
    RecordingEffects effects;
    effects.save_succeeds = false;
    effects.keep_open = false;
    ShutdownCoordinator coordinator(effects);
    coordinator.request_close();
    effects.log.clear();
    coordinator.deferred_shutdown_ready();
    EXPECT((effects.log == Log{"save", "save_failure_prompt", "destroy_views",
                               "destroy_window"}));
    EXPECT(!coordinator.state().shutdown_clean_marker_armed);
}

void a_session_end_inside_the_prompt_wins_over_keep_open() {
    RecordingEffects effects;
    effects.save_succeeds = false;
    effects.keep_open = true;
    ShutdownCoordinator coordinator(effects);
    effects.nested_end_session = &coordinator;
    coordinator.request_close();
    effects.log.clear();
    coordinator.deferred_shutdown_ready();
    EXPECT((effects.log == Log{"save", "save_failure_prompt",
                               "destroy_views:ending", "destroy_window"}));
}

void a_close_during_a_transfer_prompts_and_closes_after_it() {
    RecordingEffects effects;
    ShutdownCoordinator coordinator(effects);
    coordinator.sequence().step(ShutdownEvent::file_operation_started);
    coordinator.request_close();
    EXPECT((effects.log == Log{"transfer_prompt"}));

    effects.log.clear();
    coordinator.transfer_chosen(TransferChoice::close_after_transfer);
    // Still transferring: nothing happens until it finishes.
    EXPECT(effects.log.empty());
    coordinator.sequence().step(ShutdownEvent::file_operation_finished);
    coordinator.complete_deferred_close();
    EXPECT((effects.log ==
            Log{"dismiss_transfer_prompt", "caption:closing", "post"}));
}

void keep_open_on_the_transfer_prompt_cancels_the_close() {
    RecordingEffects effects;
    ShutdownCoordinator coordinator(effects);
    coordinator.sequence().step(ShutdownEvent::file_operation_started);
    coordinator.request_close();
    coordinator.transfer_chosen(TransferChoice::keep_open);
    coordinator.sequence().step(ShutdownEvent::file_operation_finished);
    effects.log.clear();
    coordinator.complete_deferred_close();
    EXPECT(effects.log.empty());
}

void a_drag_holds_the_close_until_it_ends() {
    RecordingEffects effects;
    ShutdownCoordinator coordinator(effects);
    coordinator.drag_changed(true);
    coordinator.request_close();
    coordinator.deferred_shutdown_ready();
    // The continuation was consumed during the drag; nothing was torn down.
    EXPECT((effects.log == Log{"caption:closing", "post"}));
    effects.log.clear();
    coordinator.drag_changed(false);
    EXPECT((effects.log == Log{"caption:closing", "post"}));
}

void a_session_end_writes_the_checkpoint_before_any_teardown() {
    RecordingEffects effects;
    ShutdownCoordinator coordinator(effects);
    coordinator.end_session_confirmed();
    EXPECT((effects.log ==
            Log{"checkpoint", "destroy_views:ending", "destroy_window"}));
    EXPECT(coordinator.state().shutdown_clean_marker_armed);
}

void a_session_end_inside_a_shell_call_writes_only_the_checkpoint() {
    RecordingEffects effects;
    ShutdownCoordinator coordinator(effects);
    coordinator.sequence().step(ShutdownEvent::shell_call_entered);
    coordinator.end_session_confirmed();
    EXPECT((effects.log == Log{"checkpoint"}));
}

void a_cancelled_session_end_allows_a_later_close() {
    RecordingEffects effects;
    ShutdownCoordinator coordinator(effects);
    coordinator.sequence().step(ShutdownEvent::shell_call_entered);
    coordinator.end_session_confirmed();
    coordinator.end_session_cancelled();
    EXPECT(!coordinator.is_shutting_down());
    EXPECT(!coordinator.state().shutdown_save_attempted);
    EXPECT((effects.log == Log{"checkpoint", "caption:normal"}));

    coordinator.sequence().step(ShutdownEvent::shell_call_left);
    coordinator.request_close();
    EXPECT((effects.log == Log{"checkpoint", "caption:normal",
                               "caption:closing", "post"}));
    coordinator.deferred_shutdown_ready();
    EXPECT(coordinator.state().closing_);
}

void a_session_end_during_a_running_close_still_writes_the_checkpoint() {
    RecordingEffects effects;
    ShutdownCoordinator coordinator(effects);
    coordinator.request_close();
    coordinator.deferred_shutdown_ready();
    effects.log.clear();
    coordinator.end_session_confirmed();
    EXPECT((effects.log == Log{"checkpoint"}));
}

}  // namespace

int main() {
    a_close_posts_then_saves_then_destroys_views_before_the_window();
    a_close_inside_a_shell_call_waits_without_posting();
    a_failed_post_retries_the_close_at_once();
    a_failed_post_without_a_live_window_stops();
    a_failed_save_asks_and_keep_open_restores_the_caption();
    a_failed_save_closes_without_the_marker_on_an_explicit_no();
    a_session_end_inside_the_prompt_wins_over_keep_open();
    a_close_during_a_transfer_prompts_and_closes_after_it();
    keep_open_on_the_transfer_prompt_cancels_the_close();
    a_drag_holds_the_close_until_it_ends();
    a_session_end_writes_the_checkpoint_before_any_teardown();
    a_session_end_inside_a_shell_call_writes_only_the_checkpoint();
    a_cancelled_session_end_allows_a_later_close();
    a_session_end_during_a_running_close_still_writes_the_checkpoint();
    return panedock::test::summary("core_shutdown_coordinator_test");
}
