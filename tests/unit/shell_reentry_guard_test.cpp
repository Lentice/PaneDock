#include "app_shell/shell_reentry_guard.h"
#include "core/shutdown.h"
#include "unit/test_reentry_effects.h"
#include "unit/test_util.h"

namespace {
using panedock::app_shell::ShellCallScope;
using panedock::app_shell::ShellReentryGuard;
using panedock::core::ShutdownEvent;
using panedock::core::ShutdownSequence;
using panedock::test::RecordingReentryEffects;

constexpr UINT kShutdownMessage = WM_APP + 54;

HWND window(int id) noexcept { return reinterpret_cast<HWND>(id); }

struct Fixture final {
    ShutdownSequence shutdown;
    RecordingReentryEffects effects;
    ShellReentryGuard guard{shutdown, effects, kShutdownMessage};
};

void test_only_the_outermost_leave_replays_in_hold_order() {
    Fixture f;
    {
        ShellCallScope outer(f.guard);
        {
            ShellCallScope inner(f.guard);
            f.guard.hold(window(1), WM_COMMAND, 7, 0, false);
        }
        EXPECT(f.effects.posted.empty());
        f.guard.hold(window(2), WM_COMMAND, 8, 0, false);
    }
    EXPECT(f.effects.posted.size() == 2);
    EXPECT(f.effects.posted[0].target == window(1));
    EXPECT(f.effects.posted[1].target == window(2));
}

void test_a_destroyed_target_is_skipped() {
    Fixture f;
    f.effects.dead_windows.push_back(window(1));
    {
        ShellCallScope call(f.guard);
        f.guard.hold(window(1), WM_COMMAND, 7, 0, false);
        f.guard.hold(window(2), WM_COMMAND, 7, 0, false);
    }
    EXPECT(f.effects.posted.size() == 1);
    EXPECT(f.effects.posted[0].target == window(2));
}

void test_held_messages_are_dropped_when_shutting_down() {
    Fixture f;
    {
        ShellCallScope call(f.guard);
        f.guard.hold(window(1), WM_COMMAND, 7, 0, false);
        (void)f.shutdown.step(ShutdownEvent::close_requested);
    }
    for (const auto& posted : f.effects.posted)
        EXPECT(posted.message != WM_COMMAND);
}

void test_close_in_nested_calls_posts_one_shutdown_after_the_outer_call() {
    Fixture f;
    f.effects.main_window = window(9);
    {
        ShellCallScope outer(f.guard);
        {
            ShellCallScope inner(f.guard);
            (void)f.shutdown.step(ShutdownEvent::close_requested);
        }
        EXPECT(f.effects.posted.empty());
    }
    EXPECT(f.effects.posted.size() == 1);
    EXPECT(f.effects.posted[0].target == window(9));
    EXPECT(f.effects.posted[0].message == kShutdownMessage);
    EXPECT(f.shutdown.state().shutdown_message_queued);
    {
        ShellCallScope again(f.guard);
    }
    EXPECT(f.effects.posted.size() == 1);
    EXPECT(f.effects.shutdowns_begun == 0);
}

void test_a_failed_shutdown_post_begins_shutdown_now() {
    Fixture f;
    f.effects.main_window = window(9);
    f.effects.post_succeeds = false;
    {
        ShellCallScope call(f.guard);
        (void)f.shutdown.step(ShutdownEvent::close_requested);
    }
    EXPECT(f.effects.shutdowns_begun == 1);
    EXPECT(!f.shutdown.state().shutdown_message_queued);
}

void test_in_shell_call_tracks_the_scope() {
    Fixture f;
    EXPECT(!f.guard.in_shell_call());
    {
        ShellCallScope outer(f.guard);
        EXPECT(f.guard.in_shell_call());
        {
            ShellCallScope inner(f.guard);
            EXPECT(f.guard.in_shell_call());
        }
        EXPECT(f.guard.in_shell_call());
    }
    EXPECT(!f.guard.in_shell_call());
}
}  // namespace

int main() {
    test_only_the_outermost_leave_replays_in_hold_order();
    test_a_destroyed_target_is_skipped();
    test_held_messages_are_dropped_when_shutting_down();
    test_close_in_nested_calls_posts_one_shutdown_after_the_outer_call();
    test_a_failed_shutdown_post_begins_shutdown_now();
    test_in_shell_call_tracks_the_scope();
    return panedock::test::summary("shell_reentry_guard");
}
