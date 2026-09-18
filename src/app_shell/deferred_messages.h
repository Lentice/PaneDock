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

// Returns false only if the message was already held: a burst of clicks during
// one Shell call must not replay as a burst of Group transitions once it
// unwinds.
inline bool hold_deferred_message(DeferredMessages& held,
                                  const DeferredMessage& message) {
    if (std::find(held.begin(), held.end(), message) != held.end())
        return false;
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
