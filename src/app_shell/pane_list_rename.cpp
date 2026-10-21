#include "app_shell/pane.h"
#include "app_shell/pane_host.h"
#include <algorithm>
#include <utility>

namespace panedock::app_shell {

bool Pane::list_edit_has_focus() const noexcept {
    return list_rename_edit_ && GetFocus() == list_rename_edit_;
}

bool Pane::handle_list_rename_key(const MSG& message, bool control, bool alt, bool shift) noexcept {
    if (!active() || message.hwnd != list_window_ ||
        message.message != WM_KEYDOWN || message.wParam != VK_F2 || control || alt || shift)
        return false;
    (void)begin_list_rename();
    return true;
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
    const std::wstring text = list_.rows[index].identity.shell_item
        ? list_.rows[index].name : list_.rows[index].identity.key;
    list_rename_ = ListRename{std::move(snapshot), list_.rows[index].identity};
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
    SendMessageW(list_rename_edit_, EM_SETSEL, 0, -1);
    return true;
} catch (...) { cancel_list_rename(); return false; }

void Pane::cancel_list_rename() noexcept {
    list_rename_.reset();
    const HWND edit = std::exchange(list_rename_edit_, nullptr);
    const bool focused = edit && GetFocus() == edit;
    if (edit) DestroyWindow(edit);
    if (focused && active() && list_window_) SetFocus(list_window_);
}

LRESULT CALLBACK Pane::list_edit_subclass(HWND window, UINT message, WPARAM wp,
                                         LPARAM lp, UINT_PTR id, DWORD_PTR data) try {
    auto* pane = reinterpret_cast<Pane*>(data);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, list_edit_subclass, id);
    if (message == WM_KILLFOCUS && !pane->list_rename_busy_) {
        pane->cancel_list_rename();
        return 0;
    }
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
    if (name.empty() || name.size() > managed_list::kMaxPath) {
        list_rename_result_ = E_INVALIDARG;
        return;
    }
    // Own row and interaction values before the first Shell pump. Never keep
    // a reference/iterator into rows or TabState across the operation.
    managed_list::Row replacement = *original;
    auto interaction = capture_managed_list_interaction();
    const auto old_identity = edit->identity;
    list_rename_busy_ = true;
    file_operations::RenameResult result;
    {
        ShellCallScope shell_call(reentry_guard());
        result = pane_host()->rename_item(*this, edit->snapshot, name);
    }
    list_rename_busy_ = false;
    if (!file_command_is_current(edit->snapshot)) return;
    list_rename_result_ = result.aborted ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : result.result;
    if (FAILED(list_rename_result_) || result.parsing_name.empty() ||
        result.leaf_name.empty() || result.display_name.empty()) {
        if (SUCCEEDED(list_rename_result_)) list_rename_result_ = E_UNEXPECTED;
        SetWindowTextW(status_bar_, L"Rename failed or canceled. Press F2 to retry; Refresh to reconcile.");
        return;
    }
    replacement.name = std::move(result.display_name);
    replacement.identity = old_identity.shell_item
        ? managed_list::ItemIdentity{result.parsing_name, true}
        : managed_list::ItemIdentity{std::move(result.leaf_name), false};
    if (replacement.child_location)
        replacement.child_location = core::ShellLocation{std::move(result.parsing_name), {}, {}};
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
    clear_list_index();
    std::erase_if(list_.rows, [&](const auto& row) {
        if (row.identity != old_identity) return false;
        list_name_bytes_ -= managed_list::owned_row_bytes(row);
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
