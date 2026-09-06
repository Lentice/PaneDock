#pragma once

#include <windows.h>

#include <cstddef>
#include <optional>

namespace panedock::app_shell {

class Pane;

// PD-189: a pane's own window proc handles the notifications its child
// controls raise, instead of forwarding them to the main window. The handlers
// stay coordinator free functions in main.cpp, so this is the seam: the pane
// proc asks the coordinator to act on the message and gets back the result to
// return, or std::nullopt when the message is none of the pane's business.
// Pane holds only PaneHost services, never an AppState pointer.
std::optional<LRESULT> handle_pane_control_message(Pane& pane, UINT message,
                                                   WPARAM wparam, LPARAM lparam);

}  // namespace panedock::app_shell
