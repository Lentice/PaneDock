#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>

#include "core/model.h"
#include "core/session.h"

namespace panedock::app_shell {

// Owns everything about *when and whether* the session file is written: the
// document being written, the directory, the dirty flag, the debounce timer
// and the durability flush. Before this type those five lived as three
// AppState fields plus a timer constant, and the rule tying them together --
// dirty means a write is owed; a successful write clears both the flag and
// the pending timer -- was re-stated at every call site.
//
// PD-214/PD-215: it also owns the Group-switch save deferral, because "a save
// refused while a Group transition runs is still owed" is a rule about when to
// write. The shutdown reducer events stay with the coordinator.
//
// The "capture" names are historical: the deferral once also kept PaneDock
// from reading the legacy Shell views' live locations mid-transition. Since
// PD-242 removed that host, accepted navigation writes TabState directly and
// nothing is captured; only the save deferral remains.

// Whether a Group transition is running (PD-206). Only
// SessionWriter::CaptureSuppression changes it.
class LocationCaptureGate final {
public:
    bool suppressed() const noexcept { return depth_ != 0; }

private:
    friend class SessionWriter;
    unsigned depth_{};
};

class SessionWriter final {
public:
    static constexpr UINT_PTR kTimerId = 0xD050;
    // Coalesces bursts of cheap model changes (tab/pane/Group switches).
    // Losing a minute of those on a crash is acceptable; WM_DESTROY still
    // flushes a dirty document on normal exit.
    static constexpr UINT kDelayMilliseconds = 60000;
    // Ceiling on the debounce: steady interaction just under the delay would
    // otherwise reset the timer forever and never write at all.
    static constexpr ULONGLONG kMaximumDirtyAgeMilliseconds = 600000;

    void set_directory(std::filesystem::path directory) {
        directory_ = std::move(directory);
    }
    const std::filesystem::path& directory() const noexcept {
        return directory_;
    }

    // Takes ownership of what read_session produced, so later writes preserve
    // any unrecognized fields it carried forward.
    void adopt(core::SessionDocument document) {
        document_ = std::move(document);
    }
    const core::SessionDocument& document() const noexcept {
        return document_;
    }

    void mark_dirty() noexcept { dirty_ = true; }
    bool dirty() const noexcept { return dirty_; }
    bool timer_armed() const noexcept { return armed_; }

    // The model the scheduled and immediate saves write. It must outlive the
    // writer's use.
    void bind(const core::ApplicationState& application) noexcept {
        application_ = &application;
    }
    // The window that owns the debounce timer; nullptr until it exists.
    void set_timer_owner(HWND owner) noexcept { timer_owner_ = owner; }

    const LocationCaptureGate& capture_gate() const noexcept { return gate_; }

    // Held across a Group transition: no save is written while the panes point
    // at a Group their lists do not show yet. A save refused while it is held
    // is rescheduled when the outermost one is released, so releasing it
    // before the transition's own save is no longer load-bearing.
    class CaptureSuppression final {
    public:
        explicit CaptureSuppression(SessionWriter& writer) noexcept
            : writer_(writer) {
            ++writer_.gate_.depth_;
        }
        ~CaptureSuppression() noexcept {
            if (--writer_.gate_.depth_ != 0 || !writer_.save_refused_) return;
            writer_.save_refused_ = false;
            writer_.schedule();
        }
        CaptureSuppression(const CaptureSuppression&) = delete;
        CaptureSuppression& operator=(const CaptureSuppression&) = delete;

    private:
        SessionWriter& writer_;
    };

    // A model change: a write is owed, debounced by the timer. If the timer
    // cannot be armed the write happens now instead.
    void schedule() noexcept;
    // Writes the bound model. While a transition holds CaptureSuppression it
    // refuses -- the save stays owed -- unless
    // `force_during_capture_suppression`, which shutdown uses to write the
    // model as it stands.
    bool save_now(bool clean_shutdown = false,
                  bool force_during_capture_suppression = false) noexcept;

    // Arms the debounce timer. Returns false when the timer could not be set,
    // which is the caller's signal to fall back to an immediate write.
    bool arm_timer(HWND owner) noexcept {
        if (owner == nullptr) return false;
        const ULONGLONG now = GetTickCount64();
        if (!armed_) {
            dirty_since_ = now;
        } else if (now - dirty_since_ >= kMaximumDirtyAgeMilliseconds) {
            // Past the ceiling: leave the already-armed timer alone so it
            // fires within one delay instead of being pushed back again.
            return true;
        }
        if (SetTimer(owner, kTimerId, kDelayMilliseconds, nullptr) == 0)
            return false;
        armed_ = true;
        return true;
    }
    void cancel_timer(HWND owner) noexcept {
        armed_ = false;
        if (owner != nullptr) KillTimer(owner, kTimerId);
    }

    // Writes `application` through core::write_session with the durability
    // flush. Leaves the dirty flag set on failure so a later attempt retries.
    bool write(const core::ApplicationState& application, bool clean_shutdown,
               HWND timer_owner) noexcept;

    // The final durable marker, written only after Shell, the parent HWND and
    // COM have gone away. Kept separate from write(): at that point there is
    // no timer to cancel and no dirty flag anyone will read again, and the
    // ordering guarantee is the whole point of the call.
    bool write_clean_marker(const core::ApplicationState& application) noexcept;

private:
    core::SessionDocument document_;
    std::filesystem::path directory_;
    const core::ApplicationState* application_{nullptr};
    HWND timer_owner_{nullptr};
    LocationCaptureGate gate_;
    bool save_refused_{};
    ULONGLONG dirty_since_{};
    bool dirty_{};
    bool armed_{};
};

}  // namespace panedock::app_shell
