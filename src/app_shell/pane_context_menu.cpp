#include "app_shell/pane.h"

namespace panedock::app_shell {
HRESULT Pane::context_menu_snapshot(POINT screen, bool background,
                                    ContextMenuSnapshot& snapshot) noexcept try {
    snapshot = {};
    if (!active() || !managed_list_active_ || !list_window_ || !realized_) return E_ABORT;
    if (!content_request_is_current(list_.request.binding) || list_navigation_pending_) return E_PENDING;
    const auto binding = content_request();
    const bool keyboard = screen.x == -1 && screen.y == -1;
    if (!background && !keyboard) {
        LVHITTESTINFO hit{};
        hit.pt = screen;
        ScreenToClient(list_window_, &hit.pt);
        const int row = ListView_HitTest(list_window_, &hit);
        background = row < 0 || !(hit.flags & LVHT_ONITEM);
        if (!background && !(ListView_GetItemState(list_window_, row, LVIS_SELECTED) & LVIS_SELECTED)) {
            ListView_SetItemState(list_window_, -1, 0, LVIS_SELECTED);
            if (!content_request_is_current(binding)) return E_ABORT;
            ListView_SetItemState(list_window_, row, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
    if (!content_request_is_current(binding)) return E_ABORT;
    if (background) {
        // Footer and empty space target the folder even if rows were selected.
        ListView_SetItemState(list_window_, -1, 0, LVIS_SELECTED);
        if (!content_request_is_current(binding)) return E_ABORT;
    }
    HRESULT hr = file_command_snapshot(background ? file_operations::Command::paste :
        file_operations::Command::copy, snapshot.target);
    if (FAILED(hr)) return hr;
    if (!file_command_is_current(snapshot.target)) return E_ABORT;
    snapshot.request.folder = snapshot.target.folder.parsing_name;
    // GetUIObjectOf requires children relative to the displayed folder.
    // Local identity is already a leaf; namespace identity is a parsing name
    // understood by that provider, never Row::name (display text).
    if (!background) {
        for (int row = ListView_GetNextItem(list_window_, -1, LVNI_SELECTED);
             row >= 0; row = ListView_GetNextItem(list_window_, row, LVNI_SELECTED)) {
            if (static_cast<std::size_t>(row) >= list_.rows.size()) return E_ABORT;
            snapshot.request.items.push_back(list_.rows[row].identity.key);
        }
    }
    if (keyboard) {
        RECT rect{};
        const int focused = ListView_GetNextItem(list_window_, -1, LVNI_FOCUSED);
        if (focused >= 0) ListView_EnsureVisible(list_window_, focused, FALSE);
        if (focused >= 0 && ListView_GetItemRect(list_window_, focused, &rect, LVIR_BOUNDS))
            screen = {rect.left, rect.bottom};
        else screen = {8, 8};
        ClientToScreen(list_window_, &screen);
    }
    snapshot.request.screen = screen;
    return file_command_is_current(snapshot.target) ? S_OK : E_ABORT;
} catch (...) { snapshot = {}; return E_OUTOFMEMORY; }

bool Pane::present_context_menu(HWND owner, ContextMenuSnapshot snapshot) noexcept try {
    if (context_menu_busy_ || !file_command_is_current(snapshot.target)) return false;
    // The scope outlives menu COM/HMENU cleanup and the injected presenter.
    ShellCallScope call(reentry_guard());
    context_menu_busy_ = true;
    struct Reset { bool& busy; ~Reset() { busy = false; } } reset{context_menu_busy_};
    const auto current = [&] { return managed_list_active_ && file_command_is_current(snapshot.target); };
    focus_content();
    if (!current()) return false;
    return context_menu_presenter_ ? context_menu_presenter_->show(owner, snapshot.request, current) :
        explorer_host::show_native_context_menu(owner, snapshot.request, current);
} catch (...) { return false; }

bool Pane::show_content_context_menu(HWND target, POINT screen) noexcept {
    if (!managed_list_active_ || !list_window_ || target != list_window_) return false;
    if (!active() || context_menu_busy_ || reentry_guard().in_shell_call()) return true;
    ContextMenuSnapshot snapshot;
    if (context_menu_snapshot(screen, false, snapshot) == S_OK)
        (void)present_context_menu(window(), std::move(snapshot));
    return true; // consume this list's request, including an unavailable target
}

bool Pane::show_folder_context_menu(HWND owner, POINT screen) noexcept try {
    if (!active() || !active_tab() || !realized_ || context_menu_busy_) return false;
    if (managed_list_active_) {
        ContextMenuSnapshot snapshot;
        if (context_menu_snapshot(screen, true, snapshot) != S_OK) return false;
        snapshot.request.align_above = true;
        return present_context_menu(owner, std::move(snapshot));
    }
    const auto request = content_request();
    focus_content();
    if (!content_request_is_current(request)) return false;
    ShellCallScope call(reentry_guard());
    return explorer_host_.show_folder_context_menu(owner, screen);
} catch (...) { return false; }
} // namespace panedock::app_shell
