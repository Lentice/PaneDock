#include "app_shell/pane.h"
#include <windowsx.h>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace panedock::app_shell {
HRESULT Pane::context_menu_snapshot(POINT screen, bool background,
                                    ContextMenuSnapshot& snapshot) noexcept try {
    snapshot = {};
    if (!active() || !list_window_ || !realized_) return E_ABORT;
    // Cache-restored rows being re-validated are this location's own rows.
    if (!content_request_is_current(list_.request.binding) ||
        (list_navigation_pending_ && !list_.updating)) return E_PENDING;
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
    // Like Explorer, Shift+right-click adds extended verbs; Shift+F10 does not.
    snapshot.request.extended = !keyboard && GetKeyState(VK_SHIFT) < 0;
    return file_command_is_current(snapshot.target) ? S_OK : E_ABORT;
} catch (...) { snapshot = {}; return E_OUTOFMEMORY; }

bool Pane::present_context_menu(HWND owner, ContextMenuSnapshot snapshot) noexcept try {
    if (context_menu_busy_ || !file_command_is_current(snapshot.target)) return false;
    // Verbs PaneDock performs itself, as Explorer's view does: decided from
    // owned values before the menu pumps, performed after it has closed.
    const auto& items = snapshot.request.items;
    std::optional<managed_list::ItemIdentity> folder_item;
    if (items.size() == 1) {
        const auto found = std::find_if(list_.rows.begin(), list_.rows.end(),
            [&](const auto& row) { return row.identity.key == items.front(); });
        if (found != list_.rows.end() && found->directory &&
            (!found->identity.shell_item || found->child_location))
            folder_item = found->identity;
    }
    enum class Deferred { none, rename, open_folder, new_folder } deferred{};
    const bool single = items.size() == 1, background = items.empty();
    snapshot.request.intercept = [&](std::wstring_view verb) {
        const auto is = [&](const wchar_t* name) {
            return CompareStringOrdinal(verb.data(), static_cast<int>(verb.size()),
                                        name, -1, TRUE) == CSTR_EQUAL;
        };
        // Without a hosting view the Shell's own rename does nothing, and a
        // folder's open verb would start a separate Explorer window.
        if (single && is(L"rename")) deferred = Deferred::rename;
        else if (folder_item && (is(L"open") || is(L"explore"))) deferred = Deferred::open_folder;
        else if (background && is(L"NewFolder")) deferred = Deferred::new_folder;
        return deferred != Deferred::none;
    };
    bool shown = false;
    {
        // The scope outlives menu COM/HMENU cleanup and the injected presenter.
        ShellCallScope call(reentry_guard());
        context_menu_busy_ = true;
        struct Reset { bool& busy; ~Reset() { busy = false; } } reset{context_menu_busy_};
        const auto current = [&] { return file_command_is_current(snapshot.target); };
        focus_content();
        if (!current()) return false;
        shown = menu_presenter().show(owner, snapshot.request, current);
    }
    if (deferred == Deferred::none || !file_command_is_current(snapshot.target)) return shown;
    if (deferred == Deferred::rename) {
        (void)begin_list_rename();
    } else if (deferred == Deferred::new_folder) {
        (void)create_new_folder();
    } else {
        const auto found = std::find_if(list_.rows.begin(), list_.rows.end(),
            [&](const auto& row) { return row.identity == *folder_item; });
        if (found != list_.rows.end())
            (void)open_list_row(static_cast<std::size_t>(found - list_.rows.begin()));
    }
    return shown;
} catch (...) { return false; }

bool Pane::invoke_default_list_rows_verb(std::span<const std::size_t> rows) noexcept try {
    if (context_menu_busy_ || rows.empty()) return false;
    FileCommandSnapshot target;
    if (file_command_snapshot(file_operations::Command::paste, target) != S_OK)
        return false;
    // Copy the Shell identities before invoking a provider that may pump the STA.
    explorer_host::ContextMenuRequest request;
    request.folder = target.folder.parsing_name;
    for (const auto row : rows) {
        if (row >= list_.rows.size()) return false;
        request.items.push_back(list_.rows[row].identity.key);
    }
    const DWORD position = GetMessagePos(); // the activating click or key, like Explorer
    request.screen = {GET_X_LPARAM(position), GET_Y_LPARAM(position)};
    if (!file_command_is_current(target)) return false;

    ShellCallScope call(reentry_guard());
    context_menu_busy_ = true;
    struct Reset { bool& busy; ~Reset() { busy = false; } } reset{context_menu_busy_};
    const auto current = [&] { return file_command_is_current(target); };
    if (!current()) return false;
    return menu_presenter().invoke_default(window_, request, current);
} catch (...) { return false; }

bool Pane::invoke_default_list_row_verb(std::size_t row) noexcept try {
    if (context_menu_busy_ || row >= list_.rows.size()) return false;
    FileCommandSnapshot target;
    if (file_command_snapshot(file_operations::Command::paste, target) != S_OK)
        return false;
    explorer_host::ContextMenuRequest request;
    request.folder = target.folder.parsing_name;
    request.items.push_back(list_.rows[row].identity.key);
    const DWORD position = GetMessagePos();
    request.screen = {GET_X_LPARAM(position), GET_Y_LPARAM(position)};
    if (!file_command_is_current(target)) return false;
    std::optional<explorer_host::FolderTarget> folder;
    {
        ShellCallScope call(reentry_guard());
        context_menu_busy_ = true;
        struct Reset { bool& busy; ~Reset() { busy = false; } } reset{context_menu_busy_};
        const auto current = [&] { return file_command_is_current(target); };
        if (!current()) return false;
        // The row's own parsing identity, resolved by the Shell: a shortcut's
        // folder target or a Shell-folder file (.zip) stays in this pane.
        folder = menu_presenter().resolve_folder_target(window_, request, current);
        if (!current()) return false;
        if (!folder) {
            // The owned request, never the row index: the pump may have re-sorted.
            return menu_presenter().invoke_default(window_, request, current);
        }
    }
    if (!file_command_is_current(target)) return false;
    core::ShellLocation location{std::move(folder->parsing_name), {}, {}};
    location.namespace_location = folder->namespace_location;
    if (!managed_list::valid_shell_location(location)) return false;
    ShellCallScope call(reentry_guard());
    (void)navigate_to(location);
    return true;
} catch (...) { return false; }

bool Pane::open_list_selection() try {
    if (!active() || !list_window_ || context_menu_busy_) return false;
    // Explorer asks before opening more than 15 items at once.
    constexpr UINT kOpenPromptMinimum = 15;
    if (const UINT count = ListView_GetSelectedCount(list_window_); count > kOpenPromptMinimum) {
        const auto binding = content_request();
        const std::wstring text = L"Are you sure you want to open these " +
            std::to_wstring(count) + L" items?";
        if (show_message(text.c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES) return false;
        // Nothing survives the box but owned identity: re-read the selection.
        if (!active() || !list_window_ || !content_request_is_current(binding)) return false;
    }
    std::vector<std::size_t> rows;
    for (int index = ListView_GetNextItem(list_window_, -1, LVNI_SELECTED); index >= 0;
         index = ListView_GetNextItem(list_window_, index, LVNI_SELECTED)) {
        if (static_cast<std::size_t>(index) >= list_.rows.size()) return false;
        rows.push_back(static_cast<std::size_t>(index));
    }
    if (rows.empty()) {
        const int focused = ListView_GetNextItem(list_window_, -1, LVNI_FOCUSED);
        if (focused >= 0) rows.push_back(static_cast<std::size_t>(focused));
    }
    if (rows.empty()) return false;
    // One row behaves as a double-click (a folder navigates in the pane).
    if (rows.size() == 1) return open_list_row(rows.front());
    return invoke_default_list_rows_verb(rows);
} catch (...) { return false; }

bool Pane::show_content_context_menu(HWND target, POINT screen) noexcept {
    if (!list_window_ || target != list_window_) return false;
    if (!active() || context_menu_busy_ || reentry_guard().in_shell_call()) return true;
    ContextMenuSnapshot snapshot;
    if (context_menu_snapshot(screen, false, snapshot) == S_OK)
        (void)present_context_menu(window(), std::move(snapshot));
    return true; // consume this list's request, including an unavailable target
}

bool Pane::show_folder_context_menu(HWND owner, POINT screen) noexcept try {
    if (!active() || !active_tab() || !realized_ || context_menu_busy_) return false;
    ContextMenuSnapshot snapshot;
    if (context_menu_snapshot(screen, true, snapshot) != S_OK) return false;
    snapshot.request.align_above = true;
    return present_context_menu(owner, std::move(snapshot));

} catch (...) { return false; }
} // namespace panedock::app_shell
