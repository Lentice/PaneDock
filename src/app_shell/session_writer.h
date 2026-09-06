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
// It deliberately does NOT own two things that look adjacent:
//   * capturing live pane locations into the model before a write, which
//     touches panes and the Shell, and
//   * the Group-switch capture guard and the shutdown reducer events,
// both of which are coordinator work. The coordinator calls in with a model
// snapshot it has already prepared.
class SessionWriter final {
public:
    static constexpr UINT_PTR kTimerId = 0xD050;
    static constexpr UINT kDelayMilliseconds = 500;

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

    // Arms the debounce timer. Returns false when the timer could not be set,
    // which is the caller's signal to fall back to an immediate write.
    bool arm_timer(HWND owner) noexcept {
        return owner != nullptr &&
               SetTimer(owner, kTimerId, kDelayMilliseconds, nullptr) != 0;
    }
    void cancel_timer(HWND owner) const noexcept {
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
    bool dirty_{};
};

}  // namespace panedock::app_shell
