#include "app_shell/pane.h"
#include "app_shell/pane_host.h"
#include "app_shell/incremental_sort.h"
#include "managed_list/size_text.h"
#include <windowsx.h>
#include <shlwapi.h>
#include <dbt.h>
#include <algorithm>
#include <cstdint>

namespace panedock::app_shell {
namespace {

std::size_t namespace_bytes(const Pane::ListSnapshot& snapshot) noexcept {
    std::size_t bytes{};
    for (const auto* location : {&snapshot.resolved_location, &snapshot.parent_location})
        if (*location) bytes += managed_list::owned_location_bytes(**location);
    return bytes;
}
// "Access is denied. (Error 5)". The UI is English: without English system text
// installed, only the code is shown rather than a message in another language.
std::wstring win32_error_text(DWORD error) {
    const std::wstring code = L"(Error " + std::to_wstring(error) + L")";
    wchar_t buffer[512]{};
    DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        error, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), buffer, ARRAYSIZE(buffer), nullptr);
    while (length > 0 && buffer[length - 1] <= L' ') // trailing CR/LF and spaces
        --length;
    return length == 0 ? code : std::wstring(buffer, length) + L" " + code;
}
bool bounded_shell_location(const core::ShellLocation& location) noexcept {
    return managed_list::valid_shell_location(location);
}
bool bounded_location(const std::optional<core::ShellLocation>& location) noexcept {
    return !location || bounded_shell_location(*location);
}

int ordinal_compare(std::wstring_view left, std::wstring_view right,
                    bool ignore_case) noexcept {
    const int result = CompareStringOrdinal(
        left.data(), static_cast<int>(left.size()),
        right.data(), static_cast<int>(right.size()), ignore_case ? TRUE : FALSE);
    if (result == CSTR_LESS_THAN) return -1;
    if (result == CSTR_GREATER_THAN) return 1;
    if (result == CSTR_EQUAL) return 0;
    const auto count = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (left[i] < right[i]) return -1;
        if (left[i] > right[i]) return 1;
    }
    return left.size() < right.size() ? -1 : (left.size() > right.size() ? 1 : 0);
}

// Explorer orders names with StrCmpLogicalW, so "file2" comes before "file10".
int natural_compare(const std::wstring& left, const std::wstring& right) noexcept {
    const int result = StrCmpLogicalW(left.c_str(), right.c_str());
    return result < 0 ? -1 : (result > 0 ? 1 : 0);
}

int compare_names(std::wstring_view left, std::wstring_view right) noexcept {
    const int folded = ordinal_compare(left, right, true);
    return folded != 0 ? folded : ordinal_compare(left, right, false);
}

std::wstring format_modified_time(std::uint64_t ticks) {
    if (ticks == 0) return {};
    FILETIME utc{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    FILETIME local{};
    SYSTEMTIME time{};
    if (!FileTimeToLocalFileTime(&utc, &local) ||
        !FileTimeToSystemTime(&local, &time)) return {};
    wchar_t date[64]{};
    wchar_t clock[64]{};
    if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr,
                       date, ARRAYSIZE(date)) == 0 ||
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &time, nullptr,
                       clock, ARRAYSIZE(clock)) == 0) return {};
    return std::wstring(date) + L" " + clock;
}

} // namespace

HRESULT Pane::navigate_content(core::ShellLocation location,
                               NavigationGeneration generation) {
    ShellCallScope shell_call(reentry_guard());
    const auto request = content_request();
    // All destinations use bounded owned Source batches, including namespace items.
    if (realized_) return start_managed_list(location, generation);
    const auto history_target = list_history_target_;
    cancel_managed_list();
    if (!content_request_is_current(request)) return E_ABORT;
    list_history_target_ = history_target;
    RECT rect{};
    GetClientRect(explorer_container_, &rect);
    if (list_window_) {
        ShowWindow(list_window_, SW_SHOW);
        realized_ = true;
        return start_managed_list(location, generation);
    }
    return realize_managed_list(rect, location, generation);
}

namespace {
// A header sends no click notification for the second click of a double-click, so a
// quick repeat on a column title would not flip the sort (PD-251). Treat a double
// click on a title as a plain click; dividers keep their auto-size double-click.
LRESULT CALLBACK header_double_click_proc(HWND header, UINT message, WPARAM wp, LPARAM lp,
                                          UINT_PTR, DWORD_PTR) {
    if (message == WM_LBUTTONDBLCLK) {
        HDHITTESTINFO hit{{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, 0, 0};
        SendMessageW(header, HDM_HITTEST, 0, reinterpret_cast<LPARAM>(&hit));
        if (hit.flags & HHT_ONHEADER) message = WM_LBUTTONDOWN;
    }
    return DefSubclassProc(header, message, wp, lp);
}

// comctl32 raises WinEvents from inside the ListView; an in-process UIA/MSAA hook
// then walks the LVS_OWNERDATA rows through the class-specific proxy, blocking the
// UI for seconds per sort/navigation. A superclass with the same window procedure
// has no proxy, so those clients see one plain window (PD-250).
bool register_managed_list_class() {
    static const bool registered = [] {
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        if (!GetClassInfoExW(nullptr, WC_LISTVIEWW, &cls)) return false;
        cls.lpszClassName = kManagedListClassName;
        cls.hInstance = GetModuleHandleW(nullptr);
        return RegisterClassExW(&cls) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    return registered;
}
}  // namespace

HRESULT Pane::realize_managed_list(const RECT& rect, const core::ShellLocation& location,
                                    NavigationGeneration generation) {
    if (!active() || !active_tab()) return E_ABORT;
    if (list_window_) return S_FALSE;
    if (!register_managed_list_class()) return HRESULT_FROM_WIN32(GetLastError());
    list_window_ = CreateWindowExW(0, kManagedListClassName, L"Folder",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
        rect.left, rect.top, rect.right-rect.left,
        rect.bottom-rect.top, explorer_container_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!list_window_) return HRESULT_FROM_WIN32(GetLastError());
    if (!SetWindowSubclass(list_window_, list_icon_subclass, 229, reinterpret_cast<DWORD_PTR>(this))) {
        derealize(); return E_FAIL;
    }
    ListView_SetExtendedListViewStyle(list_window_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
    ListView_SetCallbackMask(list_window_, LVIS_CUT | LVIS_DROPHILITED);
    SetWindowSubclass(ListView_GetHeader(list_window_), header_double_click_proc, 230, 0);
    column_ids_.clear(); applied_columns_.clear();
    apply_list_columns();
    SendMessageW(list_window_, WM_SETFONT, reinterpret_cast<WPARAM>(pane_host()->chrome_font()), FALSE);
    // STATIC containers do not route list notifications by themselves.
    if (!SetWindowSubclass(explorer_container_,
        [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) -> LRESULT {
            if (msg == WM_NOTIFY) return SendMessageW(GetParent(hwnd), msg, wp, lp);
            (void)id;
            return DefSubclassProc(hwnd, msg, wp, lp);
        }, 224, 0)) { derealize(); return E_FAIL; }
    const HRESULT drop_result = register_list_drop_target();
    if (FAILED(drop_result)) { derealize(); return drop_result; }
    realized_ = true;
    (void)start_managed_list(location, generation);
    return S_OK; // enumeration failures are visible content states

}
void Pane::use_managed_list(std::unique_ptr<managed_list::Source> source) {
    if (realized_) return;
    list_source_ = std::move(source); // create lazily after the content HWND exists
}
HANDLE Pane::content_event() const noexcept {
    return list_source_ ? list_source_->event() : nullptr;
}
core::BoundedLru<Pane::CachedList>& Pane::list_cache() noexcept {
    static core::BoundedLru<CachedList> cache{kListCacheBytes};
    return cache;
}
Pane::ListCacheStats Pane::list_cache_stats() noexcept {
    return list_cache().stats();
}
void Pane::limit_list_cache_bytes(std::size_t bytes) noexcept {
    list_cache().set_limit(std::min(bytes, kListCacheBytes));
}
void Pane::prune_list_cache(bool all) noexcept {
    list_cache().erase_if([&](const CachedList& entry) {
        if (entry.owner != this) return false;
        // PD-261: inactive Groups keep owned snapshots within the same global
        // LRU. Only the currently bound Group's tabs can invalidate its entries.
        if (!all && entry.snapshot.request.binding.group_id != pane_host()->active_group_id())
            return false;
        const bool valid = !all && pane_state() && std::any_of(
            pane_state()->tabs.begin(), pane_state()->tabs.end(), [&](const auto& tab) {
                return tab.id == entry.snapshot.request.binding.tab_id &&
                    tab.location == entry.snapshot.resolved_location.value_or(entry.snapshot.request.location);
            });
        return !valid;
    });
}
void Pane::discard_group_list_cache(const std::string& group_id) noexcept {
    list_cache().erase_if([&](const CachedList& entry) {
        return entry.snapshot.request.binding.group_id == group_id;
    });
}
bool Pane::save_list_cache(bool leaving_group) noexcept try {
    // Group activation changes the host ID before bind(); the old PaneState is
    // still alive here, and the snapshot's binding remains the outgoing ID.
    if (!leaving_group) prune_list_cache();
    if (!pane_state() || list_.updating ||
        (list_.phase != managed_list::Phase::complete && list_.phase != managed_list::Phase::empty) ||
        (!leaving_group && list_.request.binding.group_id != pane_host()->active_group_id())) return false;
    // A Group departure also admits its completed active tab, never a partial
    // enumeration or a closed tab. Full location identity remains mandatory.
    const auto found = std::find_if(pane_state()->tabs.begin(), pane_state()->tabs.end(), [&](const auto& tab) {
        return tab.id == list_.request.binding.tab_id &&
            (leaving_group || tab.id != pane_state()->active_tab_id) &&
            tab.location == list_.resolved_location.value_or(list_.request.location);
    });
    if (found == pane_state()->tabs.end()) return false;
    CachedList entry{this, {}, capture_managed_list_interaction()};
    entry.snapshot = std::move(list_);
    // Account allocated capacities, including empty property strings, binding,
    // anchors and list nodes. Inline strings are conservatively overcounted;
    // allocator bookkeeping is excluded, as with the active row/index budgets.
    const auto string_bytes = [](const auto& value) {
        return (value.capacity() + 1) * sizeof(typename std::decay_t<decltype(value)>::value_type);
    };
    std::size_t bytes = sizeof(CachedList) + sizeof(std::size_t) + 2 * sizeof(void*) +
        entry.snapshot.rows.capacity() * sizeof(managed_list::Row);
    const auto& request = entry.snapshot.request;
    for (const auto* location : {&entry.snapshot.resolved_location, &entry.snapshot.parent_location})
        if (*location) bytes += managed_list::owned_location_bytes(**location);
    bytes += string_bytes(request.binding.group_id) + string_bytes(request.binding.tab_id) +
        managed_list::owned_location_bytes(request.location);
    for (const auto& row : entry.snapshot.rows) {
        bytes += managed_list::owned_row_bytes(row);
    }
    const auto& interaction = entry.interaction;
    bytes += interaction.selected_items.capacity() * sizeof(managed_list::ItemIdentity);
    for (const auto& name : interaction.selected_items) bytes += string_bytes(name.key);
    for (const auto* name : {&interaction.focused_item, &interaction.anchor_item, &interaction.viewport_anchor_item})
        if (*name) bytes += string_bytes((**name).key);
    return list_cache().admit(std::move(entry), bytes);
} catch (...) { return false; /* Cache admission failure never prevents navigation. */ }
std::optional<Pane::CachedList> Pane::take_list_cache(const managed_list::Request& request) {
    return list_cache().take([&](const CachedList& entry) {
        return entry.owner == this && entry.snapshot.request.binding.group_id == request.binding.group_id &&
            entry.snapshot.request.binding.tab_id == request.binding.tab_id &&
            entry.snapshot.resolved_location.value_or(entry.snapshot.request.location) == request.location;
    });
}
void Pane::cancel_managed_list(bool discard_cache) noexcept {
    cancel_list_rename();
    release_list_device();
    if (discard_cache) prune_list_cache(true);
    if (list_source_) list_source_->cancel();
    cancel_list_icons();
    row_store_.clear_index();
    list_ = {};
    property_sort_pending_ = false;
    property_capacity_failed_ = false;
    property_sort_key_.clear();
    list_navigation_pending_ = false;
    list_history_target_.reset();
    list_retry_history_target_.reset();
    row_store_.recount(list_.rows);
    if (list_window_) {
        clear_managed_list_interaction();
        ListView_SetItemCountEx(list_window_, 0, 0);
    }
}
void Pane::clear_managed_list_interaction() noexcept {
    last_viewport_anchor_clamped_ = false;
    if (!list_window_) return;
    ListView_SetItemState(list_window_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(list_window_, -1);
}

Pane::ManagedListInteraction Pane::capture_managed_list_interaction() const {
    ManagedListInteraction saved;
    const auto snapshot = list_interaction_snapshot();
    saved.selected_items.reserve(snapshot.selected.size());
    for (const auto& item : snapshot.selected)
        saved.selected_items.push_back({std::wstring(item.name), item.shell_item});
    if (snapshot.focused) saved.focused_item.emplace(managed_list::ItemIdentity{
        std::wstring(snapshot.focused->name), snapshot.focused->shell_item});
    if (snapshot.anchor) saved.anchor_item.emplace(managed_list::ItemIdentity{
        std::wstring(snapshot.anchor->name), snapshot.anchor->shell_item});
    if (snapshot.viewport_anchor)
        saved.viewport_anchor_item.emplace(managed_list::ItemIdentity{
            std::wstring(snapshot.viewport_anchor->name), snapshot.viewport_anchor->shell_item});
    saved.viewport_offset = snapshot.viewport_offset;
    saved.focused_index = list_window_ ? ListView_GetNextItem(list_window_, -1, LVNI_FOCUSED) : -1;
    saved.viewport_index = list_window_ ? ListView_GetTopIndex(list_window_) : -1;
    std::sort(saved.selected_items.begin(), saved.selected_items.end(),
        [](const auto& left, const auto& right) {
            return left < right;
        });
    return saved;
}

void Pane::restore_managed_list_interaction(
    const ManagedListInteraction& interaction) noexcept {
    last_viewport_anchor_clamped_ = false;
    if (!list_window_) return;
    ListView_SetItemState(list_window_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(list_window_, -1);

    const auto index_of = [&](const std::optional<managed_list::ItemIdentity>& name) {
        if (!name) return -1;
        const auto found = std::find_if(list_.rows.begin(), list_.rows.end(),
            [&](const auto& row) { return row.identity == *name; });
        return found == list_.rows.end() ? -1 :
            static_cast<int>(found - list_.rows.begin());
    };

    for (std::size_t index = 0; index < list_.rows.size(); ++index) {
        const auto& name = list_.rows[index].identity;
        if (std::binary_search(interaction.selected_items.begin(),
                interaction.selected_items.end(), name,
                [](const auto& left, const auto& right) {
                    return left < right;
                }))
            ListView_SetItemState(list_window_, static_cast<int>(index),
                                  LVIS_SELECTED, LVIS_SELECTED);
    }

    const auto fallback = [&](int old_index) {
        return old_index < 0 || list_.rows.empty() ? -1 :
            std::min(old_index, static_cast<int>(list_.rows.size()) - 1);
    };
    int focused = index_of(interaction.focused_item);
    if (focused < 0 && interaction.focused_item) focused = fallback(interaction.focused_index);
    if (focused >= 0)
        ListView_SetItemState(list_window_, focused, LVIS_FOCUSED, LVIS_FOCUSED);
    const int anchor = index_of(interaction.anchor_item);
    // Setting native focus can create a selection mark. A missing identity
    // must clear it after focus fallback so Shift-select cannot inherit it.
    ListView_SetSelectionMark(list_window_, anchor);

    int viewport = index_of(interaction.viewport_anchor_item);
    if (viewport < 0 && interaction.viewport_anchor_item) viewport = fallback(interaction.viewport_index);
    if (viewport >= 0) {
        const int row_count = static_cast<int>(list_.rows.size());
        const int page = std::max(1, ListView_GetCountPerPage(list_window_));
        const int max_top = std::max(0, row_count - page);
        const bool end_clamped = viewport > max_top;
        if (end_clamped) {
            // A native ListView cannot scroll its last page past the final
            // real row. Put that page at its nearest legal position and keep
            // its pixel offset; never add synthetic owner-data items.
            last_viewport_anchor_clamped_ = true;
            ListView_EnsureVisible(list_window_, row_count - 1, FALSE);
        } else {
            ListView_EnsureVisible(list_window_, viewport, FALSE);
        }

        const int aligned_row = end_clamped ? ListView_GetTopIndex(list_window_) : viewport;
        RECT item{};
        RECT client{};
        if (ListView_GetItemRect(list_window_, aligned_row, &item, LVIR_BOUNDS) &&
            GetClientRect(list_window_, &client)) {
            const int current_offset = item.top - client.top;
            const int scroll = current_offset - interaction.viewport_offset;
            if (scroll != 0) ListView_Scroll(list_window_, 0, scroll);
        }

        const int actual_top = ListView_GetTopIndex(list_window_);
        RECT actual_item{};
        RECT actual_client{};
        const bool measured = actual_top >= 0 &&
            ListView_GetItemRect(list_window_, actual_top, &actual_item, LVIR_BOUNDS) &&
            GetClientRect(list_window_, &actual_client);
        const int actual_offset = measured ? actual_item.top - actual_client.top : 0;
        if (actual_top != viewport || !measured ||
            actual_offset != interaction.viewport_offset)
            last_viewport_anchor_clamped_ = true;
    }
}

// Callers that reorder under an F2 edit cancel it themselves (sort change) or
// keep it on its row (keep_list_rename_in_place after a batch).
void Pane::sort_managed_list_rows(std::size_t sorted_prefix) noexcept {
    auto* tab = active_tab();
    if (tab == nullptr) return;
    const auto property = panedock::shell_core::parse_sort_property(tab->sort_column)
        .value_or(panedock::shell_core::SortProperty::name);
    const int slot = expensive_sort_slot();
    if (list_.watch == managed_list::WatchState::recovering) return;
    if (slot >= 0) return; // expensive order commits through bounded merge steps
    const bool ascending = tab->sort_ascending;

    const auto less = [property, ascending](const managed_list::Row& left,
                                            const managed_list::Row& right) noexcept {
        // Explorer keeps folders and files apart and reverses that grouping with the
        // direction, so a descending sort lists folders last (PD-253).
        if (left.directory != right.directory) return ascending ? left.directory : right.directory;

        int primary = 0;
        bool left_empty = false;
        bool right_empty = false;
        switch (property) {
            case panedock::shell_core::SortProperty::name:
                primary = natural_compare(left.name, right.name);
                break;
            case panedock::shell_core::SortProperty::size:
                left_empty = left.directory;
                right_empty = right.directory;
                if (!left_empty && !right_empty)
                    primary = left.bytes < right.bytes ? -1 :
                        (left.bytes > right.bytes ? 1 : 0);
                break;
            case panedock::shell_core::SortProperty::date_modified:
                left_empty = left.date_modified == 0;
                right_empty = right.date_modified == 0;
                if (!left_empty && !right_empty)
                    primary = left.date_modified < right.date_modified ? -1 :
                        (left.date_modified > right.date_modified ? 1 : 0);
                break;
        }
        if (left_empty != right_empty) return !left_empty;
        if (primary != 0) return ascending ? primary < 0 : primary > 0;

        const int name_order = compare_names(left.name, right.name);
        if (name_order != 0) return name_order < 0;
        if (left.bytes != right.bytes) return left.bytes < right.bytes;
        if (left.date_modified != right.date_modified) return left.date_modified < right.date_modified;
        return left.identity < right.identity;
    };
    if (sorted_prefix > list_.rows.size()) sorted_prefix = 0;
    if (sorted_prefix == 0) {
        std::sort(list_.rows.begin(), list_.rows.end(), less);
    } else {
        const auto middle = list_.rows.begin() + static_cast<std::ptrdiff_t>(sorted_prefix);
        try {
            detail::sort_appended_and_merge(list_.rows.begin(), middle, list_.rows.end(), less);
        } catch (...) {
            // A temporary merge buffer is optional. Keep the noexcept Pane path
            // correct if it cannot be allocated by falling back to the old sort.
            std::sort(list_.rows.begin(), list_.rows.end(), less);
        }
    }
}

bool Pane::property_row_less(std::size_t left_index, std::size_t right_index) const noexcept {
    const auto& left = list_.rows[left_index];
    const auto& right = list_.rows[right_index];
    const bool ascending = active_tab()->sort_ascending;
    if (left.directory != right.directory) return ascending ? left.directory : right.directory;
    const int slot = expensive_sort_slot();
    const auto& a = left.properties[slot];
    const auto& b = right.properties[slot];
    const bool a_empty = a.state != managed_list::PropertyState::value;
    const bool b_empty = b.state != managed_list::PropertyState::value;
    if (a_empty != b_empty) return !a_empty;
    int primary = 0;
    if (!a_empty && !b_empty) {
        if (a.numeric != b.numeric) primary = a.numeric ? -1 : 1;
        else if (a.numeric) primary = a.number < b.number ? -1 : a.number > b.number ? 1 : 0;
        else primary = ordinal_compare(a.text, b.text, true);
    }
    if (primary != 0) return ascending ? primary < 0 : primary > 0;
    const int name_order = compare_names(left.name, right.name);
    if (name_order != 0) return name_order < 0;
    if (left.bytes != right.bytes) return left.bytes < right.bytes;
    if (left.date_modified != right.date_modified) return left.date_modified < right.date_modified;
    return left.identity < right.identity;
}

void Pane::update_managed_list_sort_indicator() noexcept {
    if (!list_window_) return;
    const auto* tab = active_tab();
    if (!tab) return;
    const auto property = panedock::shell_core::parse_sort_property(tab->sort_column)
        .value_or(panedock::shell_core::SortProperty::name);
    const int sorted_column = property == panedock::shell_core::SortProperty::name ? 0 :
        (property == panedock::shell_core::SortProperty::size ? 1 : 2);
    const int slot = expensive_sort_slot();
    const int actual_column = slot == 0 ? 3 : slot == 1 ? 4 : slot == 2 ? -1 : sorted_column;
    const HWND header = ListView_GetHeader(list_window_);
    if (!header) return;
    const int columns = Header_GetItemCount(header);
    for (int index = 0; index < columns; ++index) {
        HDITEMW item{};
        item.mask = HDI_FORMAT;
        if (!SendMessageW(header, HDM_GETITEMW, static_cast<WPARAM>(index),
                          reinterpret_cast<LPARAM>(&item))) continue;
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (list_column_subitem(index) == actual_column)
            item.fmt |= tab->sort_ascending ? HDF_SORTUP : HDF_SORTDOWN;
        SendMessageW(header, HDM_SETITEMW, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(&item));
    }
}

void Pane::apply_managed_list_sort() noexcept {
    cancel_list_rename();
    if (!list_window_ || !active()) return;
    try {
        const auto* tab = active_tab();
        if (tab && property_sort_key_ != tab->sort_column) {
            cancel_list_icons();
            property_sort_key_ = tab->sort_column;
            property_sort_cursor_ = 0;
            property_capacity_failed_ = false;
            for (auto& row : list_.rows) {
                const auto old_bytes = managed_list::owned_string_bytes(row.properties[2].text);
                row.properties[2] = {};
                row_store_.account(old_bytes,
                    managed_list::owned_string_bytes(row.properties[2].text));
            }
        }
        property_sort_pending_ = expensive_sort_slot() >= 0;
        property_sort_cursor_ = 0;
        property_order_.clear();
        const auto interaction = capture_managed_list_interaction();
        sort_managed_list_rows();
        if (!list_.rows.empty())
            ListView_RedrawItems(list_window_, 0,
                static_cast<int>(list_.rows.size()) - 1);
        restore_managed_list_interaction(interaction);
        update_managed_list_sort_indicator();
        finish_property_batch();
        queue_list_icon_viewport();
    } catch (...) {
        // Sorting uses bounded row data and no Shell calls. Preserve the last
        // displayed order if an interaction snapshot cannot be allocated.
    }
}

void Pane::set_managed_list_sort(
    panedock::shell_core::SortProperty property) {
    auto* tab = active_tab();
    if (!tab) return;
    const auto current = panedock::shell_core::parse_sort_property(tab->sort_column)
        .value_or(panedock::shell_core::SortProperty::name);
    if (current == property) {
        tab->sort_column = std::string(
            panedock::shell_core::sort_property_name(property));
        tab->sort_ascending = !tab->sort_ascending;
    } else {
        tab->sort_column = std::string(
            panedock::shell_core::sort_property_name(property));
        // Explorer's first click shows the newest and the largest first.
        tab->sort_ascending = property != panedock::shell_core::SortProperty::size &&
            property != panedock::shell_core::SortProperty::date_modified;
    }
    apply_managed_list_sort();
    if (pane_host()) pane_host()->schedule_session_save();
}
HRESULT Pane::start_managed_list(const core::ShellLocation& location,
                                NavigationGeneration generation) {
    if (!active() || !active_tab() || !navigation_request_is_current(generation)) return E_ABORT;
    cancel_list_rename(); // Explorer ends a label edit on navigation and Refresh
    release_list_device();
    if (!list_source_) list_source_ = managed_list::make_local_source();
    if (list_source_) list_source_->cancel();
    cancel_list_icons();
    property_sort_cursor_ = 0;
    property_sort_key_ = active_tab()->sort_column;
    property_sort_pending_ = expensive_sort_slot() >= 0;
    property_capacity_failed_ = false;
    row_store_.clear_index();
    auto cached = take_list_cache({content_request(), location});
    if (!content_request_is_current(list_.request.binding) &&
        list_.request.binding.group_id == pane_host()->active_group_id() &&
        list_.request.binding.tab_id == active_tab()->id &&
        location == list_.resolved_location.value_or(list_.request.location) &&
        !list_.updating && (list_.phase == managed_list::Phase::complete ||
                           list_.phase == managed_list::Phase::empty)) {
        // Explicit Refresh retains the active snapshot just like a cache hit.
        cached.emplace(CachedList{this, {}, capture_managed_list_interaction()});
        cached->snapshot = std::move(list_);
    }
    save_list_cache();
    list_ = { {content_request(), location}, {}, managed_list::Phase::loading, 0};
    list_.notification = managed_list::is_filesystem_location(location.parsing_name)
        ? managed_list::NotificationCapability::filesystem_watch
        : managed_list::NotificationCapability::manual_refresh;
    list_navigation_pending_ = true;
    row_store_.recount(list_.rows);
    if (list_window_) {
        clear_managed_list_interaction();
        ListView_SetItemCountEx(list_window_, 0, 0);
    }
    if (cached) {
        list_.rows = std::move(cached->snapshot.rows);
        list_.updating = true;
        list_.notification = cached->snapshot.notification;
        // Notifications were suspended while inactive: even equal size/time is
        // not evidence that optional properties or overlays are still current.
        icon_cache_.clear();
        if (icon_images_) ImageList_SetImageCount(icon_images_, 2);
        for (auto& row : list_.rows) row.properties = {};
        row_store_.recount(list_.rows);
        managed_list::Batch begin;
        begin.change = managed_list::Change::rescan_begin;
        ManagedListInteraction unused;
        apply_list_changes(begin, unused);
        if (list_.watch == managed_list::WatchState::failed) {
            list_.phase = managed_list::Phase::error;
            list_.error = list_.watch_error;
            list_.updating = false;
            resolve_list_navigation(false);
            update_managed_list();
            return S_OK;
        }
        update_managed_list();
        restore_managed_list_interaction(cached->interaction);
    }
    apply_managed_list_view_mode();
    // Loading is immediate; only a valid first batch commits location/history.
    // Mode/sort remain authoritative model values until their later slices.
    const auto request = list_.request;
    refresh_navigation_chrome();
    if (!content_request_is_current(request.binding) || !(request == list_.request)) return E_ABORT;
    if (!list_source_ || !list_source_->submit(list_.request)) {
        list_.phase = managed_list::Phase::error;
        list_.error = ERROR_NO_SYSTEM_RESOURCES;
        list_.updating = false;
        row_store_.clear_index();
        resolve_list_navigation(false);
    }
    update_managed_list();
    return S_OK;
}
void Pane::resolve_list_navigation(bool success) {
    if (!list_navigation_pending_) return;
    list_navigation_pending_ = false;
    const auto request = list_.request;
    if (success) {
        // Restore/Refresh canonicalization replaces the current history slot;
        // it is not a user navigation to an extra alias destination.
        if (active_tab() && active_tab()->location == request.location)
            set_suppress_history(true);
        navigation_complete(request.binding.generation,
            list_.resolved_location.value_or(request.location));
    } else {
        list_retry_history_target_ = list_history_target_;
        navigation_failed(request.binding.generation);
    }
}
void Pane::process_content_results() noexcept try {
    if (!list_source_) return;
    // Shutting down (or unhosted): nothing will consume these results. Drop
    // interest so the signalled event cannot keep waking the message loop.
    // A shutdown can still be called off (cancelled session end), so say
    // that this list stopped refreshing instead of leaving it silently stale.
    if (!active()) {
        list_source_->cancel();
        release_list_device();
        row_store_.clear_index();
        if (list_.phase != managed_list::Phase::idle) {
            list_.watch = managed_list::WatchState::failed;
            list_.watch_error = ERROR_CANCELLED;
            list_.updating = false;
            if (list_.phase == managed_list::Phase::loading) {
                list_.phase = managed_list::Phase::error;
                list_.error = ERROR_CANCELLED;
            }
        }
        return;
    }
    if (reentry_guard_ && reentry_guard().in_shell_call()) return;
    std::optional<ManagedListInteraction> interaction;
    bool rows_changed = false;
    // Notification deltas place their own rows (apply_list_changes); every
    // other row change re-sorts here.
    bool full_sort = false;
    std::optional<core::ShellLocation> lost_target;
    const std::size_t sorted_prefix = list_.rows.size();
    bool append_only = list_.watch != managed_list::WatchState::recovering &&
        !list_.updating;
    // At most two batches per dispatch. Leave the event signalled if more
    // arrived, and let the main loop dispatch input before consuming again.
    for (std::size_t n = 0; n < managed_list::kQueuedBatches; ++n) {
        auto batch = list_source_->take();
        if (!batch) break;
        if (!active() || !content_request_is_current(batch->request.binding) ||
            !(batch->request == list_.request)) continue;
        if (!bounded_location(batch->resolved_location) || !bounded_location(batch->parent_location) ||
            (batch->parent_outcome == managed_list::ParentOutcome::value) != batch->parent_location.has_value()) {
            list_.phase = managed_list::Phase::error;
            list_.error = ERROR_INVALID_DATA;
            list_.updating = false;
            row_store_.clear_index();
            list_source_->cancel();
            resolve_list_navigation(false);
            break;
        }
        if (batch->change == managed_list::Change::none && list_.phase == managed_list::Phase::loading &&
            batch->phase != managed_list::Phase::error && batch->phase != managed_list::Phase::limited) {
            const auto old_bytes = namespace_bytes(list_);
            auto resolved = std::move(batch->resolved_location);
            auto parent = std::move(batch->parent_location);
            const auto location_bytes = [](const auto& value) {
                return value ? managed_list::owned_location_bytes(*value) : 0;
            };
            const auto bytes = location_bytes(resolved) + location_bytes(parent);
            if (!row_store_.fits(list_.rows, old_bytes, bytes)) {
                list_.phase = managed_list::Phase::limited;
                list_.updating = false;
                row_store_.clear_index();
                list_source_->cancel();
                resolve_list_navigation(false);
                break;
            }
            row_store_.account(old_bytes, bytes);
            list_.resolved_location = std::move(resolved);
            list_.parent_location = std::move(parent);
            list_.parent_outcome = batch->parent_outcome;
            list_.notification = batch->notification;
        }
        if (batch->change == managed_list::Change::location_lost) {
            // The folder itself is gone: never keep showing its rows as content.
            if (batch->parent_location) lost_target = std::move(batch->parent_location);
            break;
        }
        if (batch->change != managed_list::Change::none) {
            if (batch->change != managed_list::Change::watching) append_only = false;
            if (list_.watch == managed_list::WatchState::failed &&
                batch->change != managed_list::Change::watch_error) continue;
            const bool commit = batch->change == managed_list::Change::delta ||
                (batch->change == managed_list::Change::rescan && batch->phase != managed_list::Phase::loading) ||
                (batch->change == managed_list::Change::watch_error &&
                    list_.watch == managed_list::WatchState::recovering);
            // During recovery rows only replace their own identity or append.
            // Existing native selection/viewport indexes stay valid. Capture
            // once at commit, just before compaction and the final stable sort.
            if (commit && !interaction) interaction.emplace(capture_managed_list_interaction());
            ManagedListInteraction unused;
            apply_list_changes(*batch, interaction ? *interaction : unused);
            rows_changed = rows_changed || commit;
            full_sort = full_sort || (commit && batch->change != managed_list::Change::delta);
            continue;
        }
        if (list_.phase != managed_list::Phase::loading) continue;
        if (batch->rows.size() > managed_list::kBatchRows ||
            std::any_of(batch->rows.begin(), batch->rows.end(), [](auto& row) {
                return !managed_list::prepare_row(row);
            })) {
            list_.phase = managed_list::Phase::error;
            list_.error = ERROR_INVALID_DATA;
            list_.updating = false;
            row_store_.clear_index();
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
        if (list_.updating) {
            append_only = false;
            const bool terminal = batch->phase != managed_list::Phase::loading;
            if (terminal && !interaction) interaction.emplace(capture_managed_list_interaction());
            batch->change = managed_list::Change::rescan;
            ManagedListInteraction unused;
            apply_list_changes(*batch, interaction ? *interaction : unused);
            // Keep loading until the scan ends; PD-232 reconciliation otherwise
            // exposes complete for intermediate recovery batches.
            list_.phase = batch->phase;
            list_.error = batch->error;
            if (list_.watch == managed_list::WatchState::failed) {
                list_.phase = managed_list::Phase::error;
                list_.error = list_.watch_error;
                list_source_->cancel();
                list_.updating = false;
                row_store_.clear_index();
            }
            if (terminal) {
                list_.updating = false;
                // Cache reconciliation is not a notification subscription.
                // UNC/mapped/provider locations may never send watching.
                if (list_.watch == managed_list::WatchState::watching)
                    list_.watch = managed_list::WatchState::inactive;
            }
            rows_changed = rows_changed || terminal;
            full_sort = full_sort || terminal;
            continue;
        }
        if (!batch->rows.empty() && list_.rows.size() < managed_list::kMaxRows && !interaction)
            interaction.emplace(capture_managed_list_interaction());
        const auto appended = row_store_.append(list_.rows, batch->rows);
        if (!appended) {
            list_.phase = managed_list::Phase::limited;
            list_source_->cancel(); break;
        }
        const std::size_t accepted = *appended;
        rows_changed = rows_changed || accepted != 0;
        full_sort = full_sort || (accepted != 0 && !append_only);
        list_.phase = accepted < batch->rows.size() ? managed_list::Phase::limited : batch->phase;
        list_.error = batch->error;
        if (list_.phase == managed_list::Phase::error || list_.phase == managed_list::Phase::limited)
            list_source_->cancel();
    }
    if (lost_target) {
        // Explorer leaves a deleted or renamed folder for its new name or the
        // nearest existing parent. The stale rows stay only if that fails.
        release_list_device();
        list_source_->cancel();
        row_store_.clear_index();
        list_.watch = managed_list::WatchState::failed;
        list_.watch_error = ERROR_PATH_NOT_FOUND;
        list_.updating = false;
        cancel_list_rename();
        update_managed_list();
        (void)navigate_to(*lost_target);
        return;
    }
    if (rows_changed) {
        cancel_list_icons();
        if (list_.watch != managed_list::WatchState::recovering && (full_sort || append_only))
            sort_managed_list_rows(full_sort ? 0 : sorted_prefix);
    }
    update_managed_list();
    if (rows_changed) {
        if (list_window_ && !list_.rows.empty())
            ListView_RedrawItems(list_window_, 0,
                static_cast<int>(list_.rows.size()) - 1);
        if (interaction) restore_managed_list_interaction(*interaction);
    }
    keep_list_rename_in_place();
    update_managed_list_sort_indicator();
    finish_property_batch();
    queue_list_icon_viewport();
    if (rows_changed) rename_pending_new_item(); // Ctrl+Shift+N / New > Folder row arrived
} catch (...) {
    cancel_list_rename();
    if (list_source_) list_source_->cancel();
    row_store_.clear_index();
    list_.phase = managed_list::Phase::error;
    list_.error = ERROR_NOT_ENOUGH_MEMORY;
    list_.updating = false;
    resolve_list_navigation(false);
    update_managed_list();
}
// Keeps the sole row snapshot, stops automatic work and requires an explicit
// Refresh: the one way a change notification or recovery gives up.
void Pane::fail_list_watch(DWORD error) noexcept {
    list_.watch = managed_list::WatchState::failed;
    list_.watch_error = error;
    if (list_source_) list_source_->cancel();
    row_store_.clear_index();
}
void Pane::apply_list_changes(managed_list::Batch& batch, ManagedListInteraction& interaction) {
    using managed_list::Change;
    using managed_list::WatchState;
    if (batch.change == Change::watch_error) {
        release_list_device();
        list_.watch = WatchState::failed; list_.watch_error = batch.error;
        row_store_.clear_index(); return;
    }
    if (batch.change == Change::watching) {
        list_.watch = WatchState::watching; list_.watch_error = 0;
        register_list_device(std::move(batch.device_handle));
        return;
    }
    if (batch.change == Change::rescan_begin) {
        list_.watch = WatchState::recovering;
        if (const DWORD error = row_store_.begin_recovery(list_.rows)) fail_list_watch(error);
        return;
    }
    if (!managed_list::batch_within_limits(batch)) {
        fail_list_watch(ERROR_INVALID_DATA); return;
    }
    // A rename is the only notification that transfers an interaction
    // identity. Deletes never implicitly select a different row.
    for (const auto& [old_leaf, new_leaf] : batch.renamed) {
        const managed_list::ItemIdentity from{old_leaf, false}, to{new_leaf, false};
        for (auto& name : interaction.selected_items) if (name == from) name = to;
        for (auto* name : {&interaction.focused_item, &interaction.anchor_item,
                           &interaction.viewport_anchor_item})
            if (*name && **name == from) *name = to;
    }
    std::sort(interaction.selected_items.begin(), interaction.selected_items.end(),
        [](const auto& a, const auto& b) { return a < b; });
    auto result = row_store_.apply(list_.rows, batch, list_.watch == WatchState::recovering);
    if (result.limited) list_.phase = managed_list::Phase::limited;
    if (result.watch_failed) fail_list_watch(*result.watch_failed);
    if (result.aborted) return;
    if (result.recovered) list_.watch = WatchState::watching;
    if (result.sorted_prefix) sort_managed_list_rows(*result.sorted_prefix);
    list_.phase = list_.rows.empty() ? managed_list::Phase::empty : managed_list::Phase::complete;
    property_sort_pending_ = expensive_sort_slot() >= 0;
    // Only images of changed identities are reloaded (including overlay-only
    // changes for which the basic size/time cache key did not change).
    forget_list_icons(std::move(result.touched));
}
void Pane::forget_list_icons(std::vector<managed_list::ItemIdentity> identities) noexcept {
    if (identities.empty() || icon_cache_.empty()) return;
    try {
        std::sort(identities.begin(), identities.end());
        for (std::size_t i = 0; i < icon_cache_.size();) {
            if (!std::binary_search(identities.begin(), identities.end(), icon_cache_[i].key.identity)) {
                ++i; continue;
            }
            // Same slot repair as LRU eviction; placeholders 0/1 never move.
            const int removed = icon_cache_[i].slot;
            if (!icon_images_ || removed < 2 || !ImageList_Remove(icon_images_, removed)) throw std::bad_alloc();
            icon_cache_.erase(icon_cache_.begin() + static_cast<std::ptrdiff_t>(i));
            for (auto& entry : icon_cache_) if (entry.slot > removed) --entry.slot;
        }
    } catch (...) {
        icon_cache_.clear();
        if (icon_images_) ImageList_SetImageCount(icon_images_, 2);
    }
}
void Pane::update_managed_list() noexcept try {
    if (list_window_ && ListView_GetItemCount(list_window_) !=
            static_cast<int>(list_.rows.size()))
        ListView_SetItemCountEx(list_window_, static_cast<int>(list_.rows.size()),
            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    // Explorer: "12 items    3 items selected  1.50 MB"; the size only when no folder
    // is selected. Runs once per posted selection burst, not per item notification.
    std::wstring text = managed_list::item_count_text(list_.rows.size());
    const int selected = list_window_ ? ListView_GetSelectedCount(list_window_) : 0;
    if (selected > 0) {
        std::optional<std::uint64_t> bytes{0};
        for (int index = ListView_GetNextItem(list_window_, -1, LVNI_SELECTED); index >= 0;
             index = ListView_GetNextItem(list_window_, index, LVNI_SELECTED)) {
            if (static_cast<std::size_t>(index) >= list_.rows.size()) continue;
            const auto& row = list_.rows[static_cast<std::size_t>(index)];
            if (row.directory) { bytes.reset(); break; }
            *bytes += row.bytes;
        }
        text += L"    " + managed_list::selection_status_text(static_cast<std::uint64_t>(selected), bytes);
    }
    switch (list_.phase) {
    case managed_list::Phase::loading: text += list_.updating ? L" - Updating..." : L" - Loading..."; break;
    case managed_list::Phase::empty: break; // "This folder is empty." is drawn in the list body
    case managed_list::Phase::error:
        text += list_.error == ERROR_NO_SYSTEM_RESOURCES
            ? L" - Worker capacity exhausted. Leave this location or Refresh to retry."
            : L" - Cannot read this location: " + win32_error_text(list_.error) + L" Refresh to retry.";
        break;
    case managed_list::Phase::limited: text += L" - Row or memory limit reached (100,000 rows / 24 MiB). Open a subfolder to continue."; break;
    default: break;
    }
    if (list_.notification == managed_list::NotificationCapability::manual_refresh)
        text += L" - Automatic refresh unavailable. Use Refresh to update.";
    if (list_.watch == managed_list::WatchState::recovering) text += L" - Recovering changes...";
    if (list_.watch == managed_list::WatchState::failed)
        text += L" - Automatic refresh unavailable: " + win32_error_text(list_.watch_error) + L" Refresh to retry.";
    if (property_sort_pending_) text += L" - Sorting properties...";
    if (property_capacity_failed_) text += L" - Properties unavailable. Refresh to retry.";
    if (status_bar_) SetWindowTextW(status_bar_, text.c_str());
    // The empty-folder text in the list body follows the phase.
    if (list_window_ && list_.rows.empty()) InvalidateRect(list_window_, nullptr, TRUE);
} catch (...) { if (status_bar_) SetWindowTextW(status_bar_, L"Cannot display folder status."); }
void Pane::queue_selection_status() noexcept {
    if (!list_window_ || selection_status_posted_) return;
    selection_status_posted_ = PostMessageW(list_window_, kSelectionStatusMessage, 0, 0) != FALSE;
}
Pane::ListInteractionSnapshot Pane::list_interaction_snapshot() const {
    ListInteractionSnapshot snapshot;
    snapshot.viewport_anchor_clamped = last_viewport_anchor_clamped_;
    if (!list_window_) return snapshot;

    const std::wstring_view location{list_.request.location.parsing_name};
    const auto identity_at = [&](int index) -> std::optional<ListItemIdentity> {
        if (index < 0 || static_cast<std::size_t>(index) >= list_.rows.size())
            return {};
        const auto& identity = list_.rows[static_cast<std::size_t>(index)].identity;
        return ListItemIdentity{location, identity.key, identity.shell_item};
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
    const int top = ListView_GetTopIndex(list_window_);
    snapshot.viewport_anchor = identity_at(top);
    RECT item{};
    RECT client{};
    if (snapshot.viewport_anchor &&
        ListView_GetItemRect(list_window_, top, &item, LVIR_BOUNDS) &&
        GetClientRect(list_window_, &client))
        snapshot.viewport_offset = item.top - client.top;
    return snapshot;
}
bool Pane::open_list_row(std::size_t row) {
    if (!active() || row >= list_.rows.size() ||
        !content_request_is_current(list_.request.binding)) return false;
    if (!list_.rows[row].directory) return invoke_default_list_row_verb(row);
    if (list_.rows[row].identity.shell_item) {
        if (!list_.rows[row].child_location) return false;
        const auto target = *list_.rows[row].child_location;
        (void)navigate_to(target);
        return true;
    }
    auto target = list_.request.location.parsing_name;
    if (!target.empty() && target.back() != L'\\') target += L'\\';
    target += list_.rows[row].identity.key;
    (void)navigate_to({std::move(target), {}, {}});
    return true;
}
std::optional<LRESULT> Pane::list_notification(NMHDR* notification) noexcept try {
    if (!notification || !list_window_ || notification->hwndFrom != list_window_) return {};
    if (notification->code == LVN_BEGINDRAG || notification->code == LVN_BEGINRDRAG) {
        const auto* begin = reinterpret_cast<const NMLISTVIEW*>(notification);
        (void)begin_list_drag(begin->ptAction, notification->code == LVN_BEGINRDRAG);
        return 0;
    }
    if (auto card = draw_list_card(notification)) return card;
    if (notification->code == LVN_GETDISPINFOW) {
        auto* info = reinterpret_cast<NMLVDISPINFOW*>(notification);
        // Owner-data lists keep only selection/focus; the callback mask asks for these.
        if ((info->item.mask & LVIF_STATE) && info->item.iItem >= 0 &&
            static_cast<std::size_t>(info->item.iItem) < list_.rows.size()) {
            const UINT mask = info->item.stateMask & (LVIS_CUT | LVIS_DROPHILITED);
            info->item.state = (info->item.state & ~mask) |
                (list_transfer_state(list_.rows[info->item.iItem]) & mask);
        }
        if (info->item.mask & LVIF_IMAGE)
            info->item.iImage = info->item.iItem < 0 ? 0 :
                list_item_image(static_cast<std::size_t>(info->item.iItem));
        if ((info->item.mask & LVIF_TEXT) && info->item.iItem >= 0 &&
            static_cast<std::size_t>(info->item.iItem) < list_.rows.size()) {
            const auto& row = list_.rows[info->item.iItem];
            // comctl32 reports the header position; hidden or moved columns change it.
            const int sub = list_column_subitem(info->item.iSubItem);
            std::wstring text;
            if (sub == 0) text = row.name;
            else if (sub == 1)
                text = row.directory ? L"" : managed_list::file_size_text(row.bytes); // Explorer leaves folder sizes blank
            else if (sub == 2)
                text = format_modified_time(row.date_modified);
            else if (sub == 5) text = row.cloud.text();
            else if (sub == 3 || sub == 4) {
                // Explorer leaves pending and missing values blank; only a value shows.
                const auto& value = row.properties[sub - 3];
                if (value.state == managed_list::PropertyState::value) {
                    if (sub == 4 && value.numeric)
                        text = managed_list::duration_text(static_cast<std::uint64_t>(value.number));
                    else text = value.numeric ? std::to_wstring(static_cast<double>(value.number)) : value.text;
                }
            }
            if (info->item.cchTextMax > 0)
                lstrcpynW(info->item.pszText, text.c_str(), info->item.cchTextMax);
        }
        return 0;
    }
    if (notification->code == LVN_COLUMNCLICK) {
        const auto* click = reinterpret_cast<const NMLISTVIEW*>(notification);
        // The notification carries the header position; columns can be hidden or moved.
        const int clicked = list_column_subitem(click->iSubItem);
        switch (clicked) {
            case 0: set_managed_list_sort(panedock::shell_core::SortProperty::name); break;
            case 1: set_managed_list_sort(panedock::shell_core::SortProperty::size); break;
            case 2: set_managed_list_sort(panedock::shell_core::SortProperty::date_modified); break;
            case 3: case 4: {
                auto* tab = active_tab();
                if (!tab) break;
                const std::string key = clicked == 3 ? "System.ItemTypeText" : "System.Media.Duration";
                tab->sort_ascending = tab->sort_column == key ? !tab->sort_ascending : true;
                tab->sort_column = key;
                apply_managed_list_sort();
                if (pane_host()) pane_host()->schedule_session_save();
                break;
            }
            default: break;
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
    // Selection changed (one item, all items, or an owner-data range): coalesce into
    // one status update after the burst instead of rescanning per notification.
    if (notification->code == LVN_ITEMCHANGED) {
        const auto* change = reinterpret_cast<const NMLISTVIEW*>(notification);
        if ((change->uChanged & LVIF_STATE) &&
            ((change->uOldState ^ change->uNewState) & LVIS_SELECTED))
            queue_selection_status();
        return 0;
    }
    if (notification->code == LVN_ODSTATECHANGED) {
        const auto* change = reinterpret_cast<const NMLVODSTATECHANGE*>(notification);
        if ((change->uOldState ^ change->uNewState) & LVIS_SELECTED) queue_selection_status();
        return 0;
    }
    if (notification->code == LVN_GETEMPTYMARKUP) {
        if (list_.phase != managed_list::Phase::empty) return FALSE;
        auto* markup = reinterpret_cast<NMLVEMPTYMARKUP*>(notification);
        markup->dwFlags = EMF_CENTERED;
        lstrcpynW(markup->szMarkup, L"This folder is empty.", ARRAYSIZE(markup->szMarkup));
        return TRUE;
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
void Pane::keep_list_rename_in_place() noexcept {
    if (!list_rename_ || !list_rename_edit_ || !list_window_) return;
    if (!file_command_is_current(list_rename_->snapshot)) { cancel_list_rename(); return; }
    const auto found = std::find_if(list_.rows.begin(), list_.rows.end(),
        [&](const auto& row) { return row.identity == list_rename_->identity; });
    if (found == list_.rows.end()) { cancel_list_rename(); return; } // removed or renamed
    const int index = static_cast<int>(found - list_.rows.begin());
    const int top = ListView_GetTopIndex(list_window_);
    RECT label{}, edit{};
    if (index < top || index >= top + std::max(1, ListView_GetCountPerPage(list_window_)) ||
        !ListView_GetItemRect(list_window_, index, &label, LVIR_LABEL) ||
        !GetWindowRect(list_rename_edit_, &edit)) {
        cancel_list_rename(); // shifted out of view: never edit a row the user cannot see
        return;
    }
    MapWindowPoints(nullptr, list_window_, reinterpret_cast<POINT*>(&edit), 2);
    if (edit.left != label.left || edit.top != label.top)
        SetWindowPos(list_rename_edit_, nullptr, label.left, label.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
void Pane::release_list_device() noexcept {
    if (list_device_notify_) UnregisterDeviceNotification(list_device_notify_);
    list_device_notify_ = nullptr;
    list_device_handle_.reset();
}
void Pane::register_list_device(std::shared_ptr<void> handle) noexcept {
    release_list_device();
    if (!handle || !list_window_) return;
    DEV_BROADCAST_HANDLE filter{};
    filter.dbch_size = sizeof(filter);
    filter.dbch_devicetype = DBT_DEVTYP_HANDLE;
    filter.dbch_handle = handle.get();
    list_device_notify_ = RegisterDeviceNotificationW(list_window_, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (list_device_notify_) list_device_handle_ = std::move(handle);
}
LRESULT Pane::list_device_change(WPARAM event, LPARAM data) noexcept {
    const auto* header = reinterpret_cast<const DEV_BROADCAST_HDR*>(data);
    if (!header || header->dbch_devicetype != DBT_DEVTYP_HANDLE || !list_device_notify_ ||
        reinterpret_cast<const DEV_BROADCAST_HANDLE*>(data)->dbch_hdevnotify != list_device_notify_)
        return TRUE;
    switch (event) {
    case DBT_DEVICEQUERYREMOVE:
        // Safely Remove / Eject: close every handle PaneDock holds on the
        // volume. Cancel makes the worker close its directory watch; the
        // registration stays to hear whether the removal went ahead. The
        // rows remain, marked as no longer refreshing. No Shell call here.
        list_device_handle_.reset();
        if (list_source_) list_source_->cancel();
        row_store_.clear_index();
        list_.watch = managed_list::WatchState::failed;
        list_.watch_error = ERROR_DEVICE_REMOVED;
        update_managed_list();
        break;
    case DBT_DEVICEQUERYREMOVEFAILED: // another program kept the volume: watch again
        release_list_device();
        if (list_window_) PostMessageW(list_window_, kListDeviceRefreshMessage, 0, 0);
        break;
    case DBT_DEVICEREMOVEPENDING:
    case DBT_DEVICEREMOVECOMPLETE:
        // The main window's volume broadcast moves this pane to This PC.
        release_list_device();
        break;
    default: break;
    }
    return TRUE;
}
void Pane::refresh_unwatched_list() noexcept try {
    if (!active() || !realized_ || !active_tab() || list_navigation_pending_ || list_.updating ||
        list_.phase == managed_list::Phase::loading || list_.phase == managed_list::Phase::idle) return;
    if (list_.notification == managed_list::NotificationCapability::manual_refresh ||
        list_.watch == managed_list::WatchState::failed)
        refresh_view();
} catch (...) {}
void Pane::volume_changed(DWORD units, bool removed) noexcept try {
    if (!active() || !realized_ || !active_tab() || list_.phase == managed_list::Phase::idle) return;
    const auto& location = list_.resolved_location.value_or(list_.request.location);
    if (managed_list::is_this_pc_location(location)) { refresh_view(); return; }
    const auto& path = location.parsing_name;
    if (!removed || path.size() < 2 || path[1] != L':') return;
    const wchar_t letter = static_cast<wchar_t>(towupper(path[0]));
    if (letter < L'A' || letter > L'Z' || !(units & (1u << (letter - L'A')))) return;
    release_list_device();
    (void)navigate_to({L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", {}, {}}); // This PC
} catch (...) {}
} // namespace panedock::app_shell
