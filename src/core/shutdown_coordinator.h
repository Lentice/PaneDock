#pragma once

#include "core/shutdown.h"

namespace panedock::core {

// Every side effect the shutdown sequence performs. The app adapter lives in
// main.cpp; tests use a recording fake. No Win32 types: the coordinator is
// core so the order of these effects is a tested fact rather
// than a source scan (PD-216).
class ShutdownEffects {
  public:
    virtual ~ShutdownEffects() = default;
    // The "Closing..." caption, or back to the normal one after keep-open.
    virtual void show_closing_caption(bool closing) noexcept = 0;
    // Whether there is a main window at all; before it exists there is
    // nothing to post a continuation to.
    virtual bool has_window() const noexcept = 0;
    // Whether the main window is still a live window.
    virtual bool window_alive() const noexcept = 0;
    // Posts the deferred shutdown continuation; false if the post failed.
    virtual bool post_deferred_shutdown() noexcept = 0;
    virtual void show_transfer_prompt() noexcept = 0;
    virtual void dismiss_transfer_prompt() noexcept = 0;
    // Captures window placement and writes the session, overriding capture
    // suppression. True if the write succeeded.
    virtual bool save_session() noexcept = 0;
    // The OS session-end checkpoint: placement plus a write that reaches no
    // Shell and no COM (PD-203). True if the write succeeded.
    virtual bool write_end_session_checkpoint() noexcept = 0;
    // Modal. True unless the user explicitly chose to close without saving.
    virtual bool ask_keep_open_after_save_failure() noexcept = 0;
    // Destroys every Shell view and everything that could reach one. The
    // parent window must still exist when this runs. `session_ending`: the OS
    // is ending the session and nobody watches the caption.
    virtual void destroy_views(bool session_ending) noexcept = 0;
    // Destroys the main window and posts the quit.
    virtual void destroy_window() noexcept = 0;
};

enum class TransferChoice { keep_open, close_after_transfer, cancel_and_close };

// Runs ShutdownSequence's actions against ShutdownEffects. The reducer decides
// what happens next; this owns in which order the effects run, and it is the
// only thing that steps the reducer for the close sequence itself.
class ShutdownCoordinator final {
  public:
    explicit ShutdownCoordinator(ShutdownEffects &effects) noexcept
        : effects_(effects) {}

    ShutdownCoordinator(const ShutdownCoordinator &) = delete;
    ShutdownCoordinator &operator=(const ShutdownCoordinator &) = delete;

    // For the passive gates (Shell calls, drags, file operations) that feed
    // the reducer without starting a step of the close sequence.
    ShutdownSequence &sequence() noexcept { return sequence_; }
    const ShutdownSequence::State &state() const noexcept {
        return sequence_.state();
    }
    bool is_shutting_down() const noexcept {
        return sequence_.is_shutting_down();
    }

    // WM_CLOSE and every internal close. A confirmed session end that is
    // still pending cannot be kept open, so it re-enters as end_session.
    void request_close() noexcept;
    // The posted continuation arrived.
    void deferred_shutdown_ready() noexcept;
    // An OLE drag target entered or left.
    void drag_changed(bool entering) noexcept;
    // A file operation or the transfer prompt may have released a close that
    // was waiting for the transfer.
    void complete_deferred_close() noexcept;
    void transfer_chosen(TransferChoice choice) noexcept;
    // WM_ENDSESSION(TRUE). Windows ends the process when the handler returns,
    // so the checkpoint is written first and teardown is best effort.
    void end_session_confirmed() noexcept;
    void end_session_cancelled() noexcept;

    void run(ShutdownAction action) noexcept;

  private:
    void begin(bool allow_keep_open) noexcept;
    void finish() noexcept;

    ShutdownEffects &effects_;
    ShutdownSequence sequence_;
};

}  // namespace panedock::core
