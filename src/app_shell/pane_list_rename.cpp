#include "app_shell/pane.h"
#include "app_shell/pane_host.h"
#include "managed_list/rename_text.h"
#include <algorithm>
#include <utility>

namespace panedock::app_shell {

bool Pane::list_edit_has_focus() const noexcept {
    return list_rename_edit_ && GetFocus() == list_rename_edit_;
}

bool Pane::list_has_key(const MSG& message) const noexcept {
    return active() && list_window_ && message.hwnd == list_window_;
}

bool Pane::perform_list_key(core::KeyAction action) noexcept {
    switch (action) {
    case core::KeyAction::begin_item_rename: (void)begin_list_rename(); return true;
    case core::KeyAction::select_all:
        select_all_list_rows(); // comctl32's list view has no select-all key
        return true;
    case core::KeyAction::new_folder: (void)create_new_folder(); return true;
    case core::KeyAction::open_selection: (void)open_list_selection(); return true;
    default: return false;
    }
}

bool Pane::begin_list_rename() noexcept try {
    if (list_rename_busy_ || list_rename_ || !active() ||
        !list_window_ || list_.phase != managed_list::Phase::complete || list_.updating || list_.watch == managed_list::WatchState::recovering ||
        (reentry_guard_ && reentry_guard().in_shell_call())) return false;
    FileCommandSnapshot snapshot;
    if (file_command_snapshot(file_operations::Command::copy, snapshot) != S_OK ||
        snapshot.items.size() != 1) return false;
    const int index = ListView_GetNextItem(list_window_, -1, LVNI_SELECTED);
    if (index < 0 || static_cast<std::size_t>(index) >= list_.rows.size()) return false;
    ListView_SetItemState(list_window_, index, LVIS_FOCUSED, LVIS_FOCUSED);
    ListView_EnsureVisible(list_window_, index, FALSE);
    RECT rect{};
    if (!ListView_GetItemRect(list_window_, index, &rect, LVIR_LABEL)) return false;
    const auto& row = list_.rows[index];
    const auto hidden = managed_list::hidden_extension(row.name, row.identity.key,
        row.identity.shell_item || row.directory);
    // With extensions hidden, Explorer edits the display name ("report", not
    // "report.txt"); otherwise the real leaf.
    const std::wstring text = row.identity.shell_item ? row.name
        : hidden.empty() ? row.identity.key : row.name;
    list_rename_ = ListRename{std::move(snapshot), row.identity, std::wstring(hidden)};
    list_rename_result_ = S_FALSE;
    list_rename_edit_ = CreateWindowExW(0, L"EDIT", text.c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        rect.left, rect.top, std::max(100L, rect.right - rect.left),
        std::max(22L, rect.bottom - rect.top), list_window_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!list_rename_edit_ || !SetWindowSubclass(list_rename_edit_, list_edit_subclass,
                                                237, reinterpret_cast<DWORD_PTR>(this))) {
        cancel_list_rename(); return false;
    }
    SendMessageW(list_rename_edit_, WM_SETFONT,
        SendMessageW(list_window_, WM_GETFONT, 0, 0), FALSE);
    SendMessageW(list_rename_edit_, EM_SETLIMITTEXT, managed_list::kMaxPath, 0);
    SetFocus(list_rename_edit_);
    // Like Explorer, preselect the name without its extension.
    SendMessageW(list_rename_edit_, EM_SETSEL, 0,
        static_cast<LPARAM>(list_rename_->hidden_extension.empty()
            ? managed_list::rename_selection_length(text, list_.rows[index].directory)
            : text.size())); // no visible extension: Explorer selects it all
    return true;
} catch (...) { cancel_list_rename(); return false; }

void Pane::select_all_list_rows() noexcept {
    if (!active() || !list_window_ || list_.rows.empty()) return;
    ListView_SetItemState(list_window_, -1, LVIS_SELECTED, LVIS_SELECTED);
}

void Pane::show_status_text(const wchar_t* text) noexcept {
    if (status_bar_ && text) SetWindowTextW(status_bar_, text);
}

bool Pane::create_new_folder() noexcept try {
    if (list_rename_busy_ || !active() || !list_window_ ||
        (reentry_guard_ && reentry_guard().in_shell_call())) return false;
    FileCommandSnapshot snapshot;
    if (file_command_snapshot(file_operations::Command::paste, snapshot) != S_OK) return false;
    cancel_list_rename();
    pending_new_item_.reset();
    list_rename_busy_ = true; // no edit can begin while the Shell pumps
    file_operations::RenameResult result;
    {
        ShellCallScope shell_call(reentry_guard());
        result = pane_host()->new_folder(*this, snapshot);
    }
    list_rename_busy_ = false;
    if (!file_command_is_current(snapshot)) return false;
    if (FAILED(result.result) || result.aborted || result.parsing_name.empty() ||
        result.leaf_name.empty()) {
        if (result.result != E_ABORT && !result.aborted &&
            result.result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
            show_status_text(L"Could not create a new folder here.");
        return false;
    }
    // The row arrives with the folder's change notification, never sooner.
    pending_new_item_ = PendingNewItem{snapshot.binding, std::move(result.parsing_name),
                                       std::move(result.leaf_name)};
    rename_pending_new_item();
    return true;
} catch (...) { list_rename_busy_ = false; return false; }

void Pane::rename_pending_new_item() noexcept {
    if (!pending_new_item_) return;
    if (!active() || !list_window_ || !content_request_is_current(pending_new_item_->binding)) {
        pending_new_item_.reset();
        return;
    }
    const auto same = [](std::wstring_view a, std::wstring_view b) {
        return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                    static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
    };
    const auto found = std::find_if(list_.rows.begin(), list_.rows.end(), [&](const auto& row) {
        return same(row.identity.key, row.identity.shell_item
            ? pending_new_item_->parsing_name : pending_new_item_->leaf_name);
    });
    if (found == list_.rows.end()) return;
    pending_new_item_.reset();
    const int index = static_cast<int>(found - list_.rows.begin());
    ListView_SetItemState(list_window_, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(list_window_, index, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(list_window_, index);
    ListView_EnsureVisible(list_window_, index, FALSE);
    (void)begin_list_rename();
}

void Pane::cancel_list_rename() noexcept {
    list_rename_.reset();
    list_rename_focus_commit_.reset();
    const HWND edit = std::exchange(list_rename_edit_, nullptr);
    const bool focused = edit && GetFocus() == edit;
    if (edit) DestroyWindow(edit);
    if (focused && active() && list_window_) SetFocus(list_window_);
}

void Pane::restore_posted_list_focus() noexcept {
    const HWND root = GetAncestor(window(), GA_ROOT);
    if (!pending_list_focus_restore_ || !list_window_ || !root ||
        list_navigation_pending_ ||
        !content_request_is_current(*pending_list_focus_restore_)) {
        list_focus_restore_posted_ = false;
        pending_list_focus_restore_.reset();
        KillTimer(list_window_, kListFocusRestoreTimerId);
        list_focus_restore_timer_used_ = false;
        list_focus_restore_activation_seen_ = false;
        return;
    }
    // A modal Shell operation can temporarily leave a destroyed popup as the
    // reported foreground window. A recent WM_ACTIVATE is the authoritative
    // signal to restore the list when that stale handle is still reported.
    const HWND foreground = GetForegroundWindow();
    const bool foreground_destroyed = !foreground || !IsWindow(foreground);
    const bool activated = list_focus_restore_activation_seen_;
    if ((foreground != root && !(activated && foreground_destroyed)) ||
        (GetFocus() != root && !activated)) {
        list_focus_restore_posted_ = false;
        if (!list_focus_restore_timer_used_) {
            list_focus_restore_timer_used_ = true;
            // One bounded retry lets transient Shell activation settle; the
            // timer is killed on its first tick and is never used for polling.
            if (!SetTimer(list_window_, kListFocusRestoreTimerId, 250, nullptr))
                list_focus_restore_timer_used_ = false;
        }
        return;
    }
    list_focus_restore_posted_ = false;
    pending_list_focus_restore_.reset();
    KillTimer(list_window_, kListFocusRestoreTimerId);
    list_focus_restore_timer_used_ = false;
    list_focus_restore_activation_seen_ = false;
    SetFocus(list_window_);
}

void Pane::restore_list_focus_after_activation() noexcept {
    const HWND root = GetAncestor(window(), GA_ROOT);
    if (!pending_list_focus_restore_ || !active() ||
        !list_window_ || !root || list_navigation_pending_ ||
        !content_request_is_current(*pending_list_focus_restore_)) return;
    list_focus_restore_activation_seen_ = true;
    if (list_focus_restore_posted_) return;
    list_focus_restore_posted_ = true;
    if (!PostMessageW(list_window_, kRestoreListFocusMessage, 0, 0))
        list_focus_restore_posted_ = false;
}

void Pane::note_list_focus_activation() noexcept {
    const HWND root = GetAncestor(window(), GA_ROOT);
    if (pending_list_focus_restore_ && active() && list_window_ && root &&
        !list_navigation_pending_ &&
        content_request_is_current(*pending_list_focus_restore_)) {
        list_focus_restore_activation_seen_ = true;
    }
}

LRESULT CALLBACK Pane::list_edit_subclass(HWND window, UINT message, WPARAM wp,
                                         LPARAM lp, UINT_PTR id, DWORD_PTR data) try {
    auto* pane = reinterpret_cast<Pane*>(data);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, list_edit_subclass, id);
    // Overrides PD-237 (cancel on focus loss): Explorer commits a label edit
    // when focus moves elsewhere. Escape, navigation, accepted batches and
    // shutdown cancel first (cancel_list_rename clears the target), so they
    // never reach this commit. The Shell call runs from a posted message,
    // never inside the focus change.
    if (message == WM_KILLFOCUS && !pane->list_rename_busy_ && pane->list_rename_ &&
        pane->list_rename_edit_ == window) {
        std::wstring text(managed_list::kMaxPath + 1, L'\0');
        text.resize(static_cast<std::size_t>(
            GetWindowTextW(window, text.data(), static_cast<int>(text.size()))));
        pane->list_rename_focus_commit_ = std::move(text);
        if (!PostMessageW(pane->list_window_, kCommitListRenameMessage, 0, 0))
            pane->cancel_list_rename();
        return DefSubclassProc(window, message, wp, lp);
    }
    if (message == WM_SETFOCUS) pane->list_rename_focus_commit_.reset();
    if (message == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        if (wp == VK_ESCAPE) pane->cancel_list_rename();
        else {
            // Bounded text copy before destroying the edit or pumping Shell.
            std::wstring text(managed_list::kMaxPath + 1, L'\0');
            const int count = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
            text.resize(count);
            pane->commit_list_rename(std::move(text));
        }
        return 0;
    }
    return DefSubclassProc(window, message, wp, lp);
} catch (...) {
    reinterpret_cast<Pane*>(data)->cancel_list_rename();
    return 0;
}

void Pane::commit_list_rename_after_focus_loss() noexcept {
    auto text = std::exchange(list_rename_focus_commit_, std::nullopt);
    if (!text || !list_rename_ || list_rename_busy_) return;
    commit_list_rename(std::move(*text));
}

void Pane::commit_list_rename(std::wstring name) noexcept try {
    auto edit = std::exchange(list_rename_, {});
    cancel_list_rename();
    if (!edit || list_rename_busy_ || (reentry_guard_ && reentry_guard().in_shell_call()) ||
        !file_command_is_current(edit->snapshot)) return;
    const auto original = [&]() -> std::optional<managed_list::Row> {
        const auto found = std::find_if(list_.rows.begin(), list_.rows.end(),
            [&](const auto& row) { return row.identity == edit->identity; });
        return found == list_.rows.end() ? std::nullopt : std::optional{*found};
    }();
    if (!original) return;
    name = managed_list::restore_hidden_extension(std::move(name), edit->hidden_extension);
    if (name.empty() || name.size() > managed_list::kMaxPath) {
        list_rename_result_ = E_INVALIDARG;
        return;
    }
    // Explorer commits an unchanged name (Enter or focus loss) silently. The
    // comparison is exact: a case-only change is a real rename.
    if (name == (edit->identity.shell_item ? original->name : edit->identity.key)) {
        list_rename_result_ = S_FALSE;
        return;
    }
    // Own row and interaction values before the first Shell pump. Never keep
    // a reference/iterator into rows or TabState across the operation.
    managed_list::Row replacement = *original;
    auto interaction = capture_managed_list_interaction();
    const auto old_identity = edit->identity;
    pending_list_focus_restore_ = edit->snapshot.binding;
    list_focus_restore_posted_ = false;
    list_focus_restore_timer_used_ = false;
    list_focus_restore_activation_seen_ = false;
    KillTimer(list_window_, kListFocusRestoreTimerId);
    list_rename_busy_ = true;
    file_operations::RenameResult result;
    {
        ShellCallScope shell_call(reentry_guard());
        result = pane_host()->rename_item(*this, edit->snapshot, name);
    }
    list_rename_busy_ = false;
    if (!content_request_is_current(edit->snapshot.binding)) {
        pending_list_focus_restore_.reset();
        list_focus_restore_posted_ = false;
        list_focus_restore_activation_seen_ = false;
        return;
    }
    if (!list_focus_restore_posted_) {
        list_focus_restore_posted_ = true;
        if (!PostMessageW(list_window_, kRestoreListFocusMessage, 0, 0)) {
            list_focus_restore_posted_ = false;
            pending_list_focus_restore_.reset();
        }
    }
    if (!file_command_is_current(edit->snapshot)) return;
    list_rename_result_ = result.aborted ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : result.result;
    if (FAILED(list_rename_result_) || result.parsing_name.empty() ||
        result.leaf_name.empty() || result.display_name.empty()) {
        if (SUCCEEDED(list_rename_result_)) list_rename_result_ = E_UNEXPECTED;
        SetWindowTextW(status_bar_, L"Rename failed or canceled. Press F2 to retry; Refresh to reconcile.");
        return;
    }
    // Overflow recovery owns the row index until its terminal batch; the
    // still-armed watch reports this rename once the folder is reconciled.
    if (list_.watch == managed_list::WatchState::recovering) return;
    replacement.name = std::move(result.display_name);
    replacement.identity = old_identity.shell_item
        ? managed_list::ItemIdentity{result.parsing_name, true}
        : managed_list::ItemIdentity{std::move(result.leaf_name), false};
    if (replacement.child_location) {
        auto& location = *replacement.child_location;
        // Rename changes the item name, not the namespace it was opened from.
        // In particular, Up from a merged Desktop child must still reach Desktop.
        // Only reuse a filesystem fallback when the Shell returned that identity;
        // a different old fallback may be stale and will be resolved on navigation.
        location.fallback_path = location.fallback_path == location.parsing_name
            ? result.parsing_name : std::wstring{};
        location.parsing_name = std::move(result.parsing_name);
    }
    replacement.properties = {};
    if (!managed_list::prepare_row(replacement)) {
        list_rename_result_ = E_INVALIDARG;
        return;
    }
    const auto new_identity = replacement.identity;
    for (auto& identity : interaction.selected_items)
        if (identity == old_identity) identity = new_identity;
    const auto translate = [&](auto& identity) {
        if (identity && *identity == old_identity) identity = new_identity;
    };
    translate(interaction.focused_item);
    translate(interaction.anchor_item);
    translate(interaction.viewport_anchor_item);
    std::sort(interaction.selected_items.begin(), interaction.selected_items.end());
    // Use the notification merge's bounded admission and identity deduplication.
    // A later rename notification may remove the already absent old key and
    // replace the new row, but can never append a duplicate of this identity.
    row_store_.clear_index();
    std::erase_if(list_.rows, [&](const auto& row) {
        if (row.identity != old_identity) return false;
        row_store_.account(managed_list::owned_row_bytes(row), 0);
        return true;
    });
    managed_list::Batch batch;
    batch.change = managed_list::Change::delta;
    batch.rows.push_back(std::move(replacement));
    apply_list_changes(batch, interaction);
    cancel_list_icons();
    sort_managed_list_rows();
    update_managed_list();
    restore_managed_list_interaction(interaction);
    queue_list_icon_viewport();
} catch (...) {
    list_rename_busy_ = false;
    list_rename_result_ = E_OUTOFMEMORY;
}

} // namespace panedock::app_shell
