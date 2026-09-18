#pragma once

#include <windows.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace panedock::app_shell {

// A message the window proc refused to run because a Shell call is pumping our
// message loop. PD-205: it is *held*, not re-posted -- a message posted back
// into that pump is re-dispatched immediately, refuses again and re-posts,
// which is a busy loop for the whole duration of the Shell call.
struct DeferredMessage final {
    // The original target. These messages belong to the main window, a pane
    // window, a tab strip subclass or the sidebar; re-posting them all to the
    // main window would silently reroute them.
    HWND target;
    UINT message;
    WPARAM wparam;
    LPARAM lparam;

    friend bool operator==(const DeferredMessage&,
                           const DeferredMessage&) noexcept = default;
};

using DeferredMessages = std::vector<DeferredMessage>;

// Whether a message's payload is only meaningful at the instant it was posted.
// A pointer message carries cursor coordinates and a hover message carries a
// generation, so a queue of them replays a trail of positions the user has
// already left -- that reproduces no intent at all, and each replayed click can
// start another slow navigation. Only the last one means anything (PD-212).
//
// This is sound because the message loop is single-threaded: nothing runs
// between holding these and releasing them, so we decide the replay ourselves
// and "the last one" is exactly what the user did last. See AGENTS.md.
inline bool deferred_message_replaces_previous(UINT message) noexcept {
    return message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK ||
           message == WM_LBUTTONUP || message == WM_CONTEXTMENU;
}

// Holds a message the window proc refused, and reports whether it became a new
// entry. `replaces` messages overwrite the payload of the entry already held
// for the same (target, message) -- keeping its queue position, so the order
// still reflects when that interaction first arrived. Everything else is
// deduplicated on full equality: a burst of identical commands must not replay
// as a burst of Group transitions once the Shell call unwinds.
//
// Callers pass `replaces` rather than letting this decide, because the hover
// message is an app-private WM_APP id that does not belong in this header.
inline bool hold_deferred_message(DeferredMessages& held,
                                  const DeferredMessage& message,
                                  bool replaces) {
    if (replaces) {
        for (DeferredMessage& entry : held) {
            if (entry.target != message.target ||
                entry.message != message.message)
                continue;
            entry.wparam = message.wparam;
            entry.lparam = message.lparam;
            return false;
        }
    } else if (std::find(held.begin(), held.end(), message) != held.end()) {
        return false;
    }
    held.push_back(message);
    return true;
}

// Empties the queue and hands the contents over, so a message dispatched from
// the released batch can hold a new one without invalidating the iteration.
inline DeferredMessages take_deferred_messages(DeferredMessages& held) {
    DeferredMessages taken;
    taken.swap(held);
    return taken;
}

}  // namespace panedock::app_shell
