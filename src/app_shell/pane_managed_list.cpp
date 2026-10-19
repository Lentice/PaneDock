#include "app_shell/pane.h"
#include "app_shell/pane_host.h"
#include <algorithm>

namespace panedock::app_shell {
HRESULT Pane::navigate_content(core::ShellLocation location,
                               NavigationGeneration generation) {
    ShellCallScope shell_call(reentry_guard());
    const auto request = content_request();
    // Syntactic routing only: no drive/provider probing on the UI STA.
    // Virtual/UNC identities remain intact on the legacy path until PD-234.
    const auto& path = location.parsing_name;
    const bool local = path.size() >= 3 && path[1] == L':' && path[2] == L'\\';
    if (local == managed_list_active_ && (local || realized_))
        return local ? start_managed_list(location, generation)
                     : explorer_host_.navigate(location, generation);

    const auto history_target = list_history_target_;
    // Only content is replaced. Pane and chrome HWNDs retain their identities.
    // Destroy pumps, so invalidate the old request before entering it and
    // check the new binding again before creating its successor.
    cancel_managed_list();
    managed_list_active_ = local;
    explorer_host_.destroy();
    if (!content_request_is_current(request)) return E_ABORT;
    list_history_target_ = history_target;
    if (list_window_) ShowWindow(list_window_, local ? SW_SHOW : SW_HIDE);
    RECT rect{};
    GetClientRect(explorer_container_, &rect);
    if (local) {
        if (list_window_) { realized_ = true; return start_managed_list(location, generation); }
        return realize_managed_list(rect, location, generation);
    }
    // Use the same generation for initial BrowseToObject and its callbacks.
    // Configure callbacks before Initialize, which can complete synchronously.
    explorer_host_.set_shell_call_callback(this, [](void* context, bool entering) noexcept {
        auto& guard = static_cast<Pane*>(context)->reentry_guard();
        if (entering) guard.enter(); else guard.leave();
    });
    explorer_host_.set_navigation_callback([this](auto token, const auto& target) {
        if (!managed_list_active_) navigation_complete(token, target);
    });
    explorer_host_.set_navigation_failed_callback([this](auto token) {
        if (!managed_list_active_) navigation_failed(token);
    });
    const auto hr = explorer_host_.initialize(explorer_container_, rect, location, generation);
    if (!content_request_is_current(request)) return E_ABORT;
    realized_ = SUCCEEDED(hr);
    if (FAILED(hr)) {
        explorer_host_.destroy();
        if (content_request_is_current(request)) navigation_failed(generation);
        return hr;
    }
    return configure_realized_view();
}

HRESULT Pane::realize_managed_list(const RECT& rect, const core::ShellLocation& location,
                                    NavigationGeneration generation) {
    if (!active() || !active_tab()) return E_ABORT;
    if (list_window_) return S_FALSE;
    list_window_ = CreateWindowExW(0, WC_LISTVIEWW, L"Local folder",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
        rect.left, rect.top, rect.right-rect.left,
        rect.bottom-rect.top, explorer_container_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!list_window_) return HRESULT_FROM_WIN32(GetLastError());
    ListView_SetExtendedListViewStyle(list_window_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = const_cast<wchar_t*>(L"Name"); column.cx = 320;
    ListView_InsertColumn(list_window_, 0, &column);
    column.pszText = const_cast<wchar_t*>(L"Size / Type"); column.cx = 160;
    ListView_InsertColumn(list_window_, 1, &column);
    SendMessageW(list_window_, WM_SETFONT, reinterpret_cast<WPARAM>(pane_host()->chrome_font()), FALSE);
    // STATIC containers do not route list notifications by themselves.
    if (!SetWindowSubclass(explorer_container_,
        [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) -> LRESULT {
            if (msg == WM_NOTIFY) return SendMessageW(GetParent(hwnd), msg, wp, lp);
            (void)id;
            return DefSubclassProc(hwnd, msg, wp, lp);
        }, 224, 0)) { derealize(); return E_FAIL; }
    realized_ = true;
    (void)start_managed_list(location, generation);
    return S_OK; // enumeration failures are visible content states

}
void Pane::use_managed_list(std::unique_ptr<managed_list::Source> source) {
    if (realized_) return;
    managed_list_enabled_ = true;
    managed_list_active_ = true;
    list_source_ = std::move(source); // create lazily after the content HWND exists
}
HANDLE Pane::content_event() const noexcept {
    return managed_list_enabled_ && list_source_ ? list_source_->event() : nullptr;
}
void Pane::cancel_managed_list() noexcept {
    if (!managed_list_enabled_) return;
    if (list_source_) list_source_->cancel();
    list_ = {};
    list_navigation_pending_ = false;
    list_history_target_.reset();
    pending_list_route_.reset();
    list_name_bytes_ = 0;
    if (list_window_) {
        clear_managed_list_interaction();
        ListView_SetItemCountEx(list_window_, 0, 0);
    }
}
void Pane::clear_managed_list_interaction() noexcept {
    if (!list_window_) return;
    ListView_SetItemState(list_window_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(list_window_, -1);
}
HRESULT Pane::start_managed_list(const core::ShellLocation& location,
                                NavigationGeneration generation) {
    if (!active() || !active_tab() || !navigation_request_is_current(generation)) return E_ABORT;
    if (!list_source_) list_source_ = managed_list::make_local_source();
    if (list_source_) list_source_->cancel();
    list_name_bytes_ = 0;
    list_ = { {content_request(), location}, {}, managed_list::Phase::loading, 0};
    if (list_window_) {
        clear_managed_list_interaction();
        ListView_SetItemCountEx(list_window_, 0, 0);
    }
    list_navigation_pending_ = true;
    // Loading is immediate; only a valid first batch commits location/history.
    // Mode/sort remain authoritative model values until their later slices.
    const auto request = list_.request;
    refresh_navigation_chrome();
    if (!content_request_is_current(request.binding) || !(request == list_.request)) return E_ABORT;
    if (!list_source_ || !list_source_->submit(list_.request)) {
        list_.phase = managed_list::Phase::error;
        list_.error = ERROR_NO_SYSTEM_RESOURCES;
        resolve_list_navigation(false);
    }
    update_managed_list();
    return S_OK;
}
void Pane::resolve_list_navigation(bool success) {
    if (!list_navigation_pending_) return;
    list_navigation_pending_ = false;
    const auto request = list_.request;
    if (success) navigation_complete(request.binding.generation, request.location);
    else navigation_failed(request.binding.generation);
}
void Pane::process_content_results() noexcept try {
    if (!list_source_) return;
    // At most two batches per dispatch. Leave the event signalled if more
    // arrived, and let the main loop dispatch input before consuming again.
    for (std::size_t n = 0; n < managed_list::kQueuedBatches; ++n) {
        auto batch = list_source_->take();
        if (!batch) break;
        if (!active() || !content_request_is_current(batch->request.binding) ||
            !(batch->request == list_.request)) continue;
        if (list_.phase != managed_list::Phase::loading) continue;
        if (batch->rows.size() > managed_list::kBatchRows ||
            std::any_of(batch->rows.begin(), batch->rows.end(), [](const auto& row) {
                return row.name.empty() || row.name.size() > managed_list::kMaxName ||
                    row.name == L"." || row.name == L".." ||
                    row.name.find_first_of(L"\\/:*?\"") != std::wstring::npos;
            })) {
            list_.phase = managed_list::Phase::error;
            list_.error = ERROR_INVALID_DATA;
            list_source_->cancel();
            resolve_list_navigation(false);
            break;
        }
        const auto request = batch->request;
        if (!batch->rows.empty() || batch->phase == managed_list::Phase::empty ||
            batch->phase == managed_list::Phase::complete)
            resolve_list_navigation(true);
        else if (batch->phase == managed_list::Phase::error)
            resolve_list_navigation(false);
        // Session/chrome callbacks may re-enter and replace the tab or Group.
        if (!content_request_is_current(request.binding) || !(request == list_.request)) continue;
        const auto count = std::min(batch->rows.size(), managed_list::kMaxRows-list_.rows.size());
        const auto required = list_.rows.size() + count;
        if (required > list_.rows.capacity()) {
            auto capacity = std::min(managed_list::kMaxRows,
                std::max(required, list_.rows.capacity()*2));
            if (list_name_bytes_ + capacity*sizeof(managed_list::Row) > managed_list::kMaxRowBytes)
                capacity = required;
            if (list_name_bytes_ + capacity*sizeof(managed_list::Row) > managed_list::kMaxRowBytes) {
                list_.phase = managed_list::Phase::limited;
                list_source_->cancel(); break;
            }
            list_.rows.reserve(capacity);
        }
        std::size_t accepted = 0;
        for (; accepted < count; ++accepted) {
            const auto bytes = (batch->rows[accepted].name.capacity()+1)*sizeof(wchar_t);
            if (list_name_bytes_ + bytes + list_.rows.capacity()*sizeof(managed_list::Row) >
                managed_list::kMaxRowBytes) break;
            list_name_bytes_ += bytes;
            list_.rows.push_back(std::move(batch->rows[accepted]));
        }
        list_.phase = accepted < batch->rows.size() ? managed_list::Phase::limited : batch->phase;
        list_.error = batch->error;
        if (list_.phase != managed_list::Phase::loading) list_source_->cancel();
    }
    update_managed_list();
} catch (...) {
    if (list_source_) list_source_->cancel();
    list_.phase = managed_list::Phase::error;
    list_.error = ERROR_NOT_ENOUGH_MEMORY;
    resolve_list_navigation(false);
    update_managed_list();
}
void Pane::update_managed_list() noexcept try {
    if (!managed_list_active_) return;
    if (list_window_ && ListView_GetItemCount(list_window_) !=
            static_cast<int>(list_.rows.size()))
        ListView_SetItemCountEx(list_window_, static_cast<int>(list_.rows.size()),
            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    std::wstring text = std::to_wstring(list_.rows.size()) + L" items";
    switch (list_.phase) {
    case managed_list::Phase::loading: text += L" - Loading..."; break;
    case managed_list::Phase::empty: text = L"This folder is empty."; break;
    case managed_list::Phase::error:
        text += list_.error == ERROR_NO_SYSTEM_RESOURCES
            ? L" - Worker unavailable. Refresh to retry."
            : L" - Cannot read this local folder. Refresh to retry. Error " + std::to_wstring(list_.error);
        break;
    case managed_list::Phase::limited: text += L" - Row or memory limit reached (100,000 rows / 24 MiB). Open a subfolder to continue."; break;
    default: break;
    }
    if (status_bar_) SetWindowTextW(status_bar_, text.c_str());
} catch (...) { if (status_bar_) SetWindowTextW(status_bar_, L"Cannot display folder status."); }
Pane::ListInteractionSnapshot Pane::list_interaction_snapshot() const {
    ListInteractionSnapshot snapshot;
    if (!managed_list_active_ || !list_window_) return snapshot;

    const std::wstring_view location{list_.request.location.parsing_name};
    const auto identity_at = [&](int index) -> std::optional<ListItemIdentity> {
        if (index < 0 || static_cast<std::size_t>(index) >= list_.rows.size())
            return {};
        return ListItemIdentity{location, list_.rows[static_cast<std::size_t>(index)].name};
    };
    for (int index = ListView_GetNextItem(list_window_, -1, LVNI_SELECTED);
         index >= 0;
         index = ListView_GetNextItem(list_window_, index, LVNI_SELECTED)) {
        if (auto identity = identity_at(index))
            snapshot.selected.push_back(*identity);
    }
    snapshot.focused = identity_at(
        ListView_GetNextItem(list_window_, -1, LVNI_FOCUSED));
    snapshot.anchor = identity_at(ListView_GetSelectionMark(list_window_));
    return snapshot;
}
bool Pane::open_list_row(std::size_t row) {
    if (!active() || !managed_list_enabled_ || row >= list_.rows.size() ||
        !content_request_is_current(list_.request.binding) || !list_.rows[row].directory) return false;
    auto target = list_.request.location.parsing_name;
    if (!target.empty() && target.back() != L'\\') target += L'\\';
    target += list_.rows[row].name;
    (void)navigate_to({std::move(target), {}, {}});
    return true;
}
std::optional<LRESULT> Pane::list_notification(NMHDR* notification) noexcept try {
    if (!notification || !list_window_ || notification->hwndFrom != list_window_) return {};
    if (notification->code == LVN_GETDISPINFOW) {
        auto* info = reinterpret_cast<NMLVDISPINFOW*>(notification);
        if ((info->item.mask & LVIF_TEXT) && info->item.iItem >= 0 &&
            static_cast<std::size_t>(info->item.iItem) < list_.rows.size()) {
            const auto& row = list_.rows[info->item.iItem];
            const auto text = info->item.iSubItem == 0 ? row.name :
                (row.directory ? std::wstring(L"Folder") : std::to_wstring(row.bytes) + L" bytes");
            if (info->item.cchTextMax > 0)
                lstrcpynW(info->item.pszText, text.c_str(), info->item.cchTextMax);
        }
        return 0;
    }
    if (notification->code == LVN_ODFINDITEMW) {
        const auto* find = reinterpret_cast<const NMLVFINDITEMW*>(notification);
        const auto& search = find->lvfi;
        if (!search.psz || !(search.flags & (LVFI_STRING | LVFI_PARTIAL | LVFI_SUBSTRING)))
            return -1;

        const int count = static_cast<int>(list_.rows.size());
        if (count == 0) return -1;
        const int start = std::clamp(find->iStart, -1, count - 1);
        const bool partial = (search.flags & (LVFI_PARTIAL | LVFI_SUBSTRING)) != 0;
        const bool wrap = (search.flags & LVFI_WRAP) != 0;
        const std::wstring_view needle{search.psz};
        for (int offset = 1; offset <= count; ++offset) {
            int index = start + offset;
            if (index >= count) {
                if (!wrap) break;
                index %= count;
            }
            const std::wstring_view name{list_.rows[static_cast<std::size_t>(index)].name};
            if (needle.empty()) return index;
            if (partial && name.size() < needle.size()) continue;
            if (!partial && name.size() != needle.size()) continue;
            if (CompareStringOrdinal(name.data(), static_cast<int>(needle.size()),
                                     needle.data(), static_cast<int>(needle.size()), TRUE) == CSTR_EQUAL)
                return index;
        }
        return -1;
    }
    if (notification->code == NM_DBLCLK) {
        const auto* click = reinterpret_cast<NMITEMACTIVATE*>(notification);
        if (click->iItem >= 0) open_list_row(static_cast<std::size_t>(click->iItem));
        return 0;
    }
    // The ListView owns selection state. Read it on demand through the Pane
    // snapshot instead of reentering the control during state notifications.
    return 0;
} catch (...) { return 0; }
} // namespace panedock::app_shell
