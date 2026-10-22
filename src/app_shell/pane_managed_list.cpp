#include "app_shell/pane.h"
#include "app_shell/pane_host.h"
#include "managed_list/size_text.h"
#include <windowsx.h>
#include <shlwapi.h>
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
        WNDCLASSEXW cls{sizeof(cls)};
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
    ListView_SetExtendedListViewStyle(list_window_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowSubclass(ListView_GetHeader(list_window_), header_double_click_proc, 230, 0);
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
    const std::array<std::pair<const wchar_t*, int>, 6> columns{{
        {L"Name", 320}, {L"Size", 160}, {L"Date Modified", 180},
        {L"Type", 160}, {L"Duration", 120}, {L"Availability", 200}}};
    for (int index = 0; index < static_cast<int>(columns.size()); ++index) {
        column.pszText = const_cast<wchar_t*>(columns[index].first);
        column.cx = columns[index].second;
        column.iSubItem = index;
        column.fmt = index == 1 ? LVCFMT_RIGHT : LVCFMT_LEFT; // Size aligns on its unit
        ListView_InsertColumn(list_window_, index, &column);
    }
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
Pane::ListCache& Pane::list_cache() noexcept {
    static ListCache cache;
    return cache;
}
Pane::ListCacheStats Pane::list_cache_stats() noexcept {
    auto stats = list_cache().stats;
    stats.entries = list_cache().entries.size();
    return stats;
}
void Pane::limit_list_cache_bytes(std::size_t bytes) noexcept {
    auto& cache = list_cache();
    cache.stats.limit = std::min<std::size_t>(bytes, 32 * 1024 * 1024);
    while (cache.stats.bytes > cache.stats.limit && !cache.entries.empty()) {
        cache.stats.bytes -= cache.entries.front().bytes;
        cache.entries.pop_front();
        ++cache.stats.evictions;
    }
}
void Pane::prune_list_cache(bool all) noexcept {
    auto& cache = list_cache();
    std::erase_if(cache.entries, [&](const auto& entry) {
        if (entry.owner != this) return false;
        const bool valid = !all && pane_state() && std::any_of(
            pane_state()->tabs.begin(), pane_state()->tabs.end(), [&](const auto& tab) {
                return tab.id == entry.snapshot.request.binding.tab_id &&
                    tab.location == entry.snapshot.resolved_location.value_or(entry.snapshot.request.location);
            });
        if (valid) return false;
        cache.stats.bytes -= entry.bytes;
        return true;
    });
}
void Pane::save_list_cache() noexcept try {
    prune_list_cache();
    if (!pane_state() || list_.updating ||
        (list_.phase != managed_list::Phase::complete && list_.phase != managed_list::Phase::empty) ||
        list_.request.binding.group_id != pane_host()->active_group_id()) return;
    // Only a still-existing inactive tab at this exact location may retain rows.
    const auto found = std::find_if(pane_state()->tabs.begin(), pane_state()->tabs.end(), [&](const auto& tab) {
        return tab.id == list_.request.binding.tab_id && tab.id != pane_state()->active_tab_id &&
            tab.location == list_.resolved_location.value_or(list_.request.location);
    });
    if (found == pane_state()->tabs.end()) return;
    CachedList entry{this, {}, capture_managed_list_interaction(), 0};
    entry.snapshot = std::move(list_);
    // Account allocated capacities, including empty property strings, binding,
    // anchors and list nodes. Inline strings are conservatively overcounted;
    // allocator bookkeeping is excluded, as with the active row/index budgets.
    const auto string_bytes = [](const auto& value) {
        return (value.capacity() + 1) * sizeof(typename std::decay_t<decltype(value)>::value_type);
    };
    entry.bytes = sizeof(CachedList) + 2 * sizeof(void*) +
        entry.snapshot.rows.capacity() * sizeof(managed_list::Row);
    const auto& request = entry.snapshot.request;
    for (const auto* location : {&entry.snapshot.resolved_location, &entry.snapshot.parent_location})
        if (*location) entry.bytes += managed_list::owned_location_bytes(**location);
    entry.bytes += string_bytes(request.binding.group_id) + string_bytes(request.binding.tab_id) +
        managed_list::owned_location_bytes(request.location);
    for (const auto& row : entry.snapshot.rows) {
        entry.bytes += managed_list::owned_row_bytes(row);
    }
    const auto& interaction = entry.interaction;
    entry.bytes += interaction.selected_items.capacity() * sizeof(managed_list::ItemIdentity);
    for (const auto& name : interaction.selected_items) entry.bytes += string_bytes(name.key);
    for (const auto* name : {&interaction.focused_item, &interaction.anchor_item, &interaction.viewport_anchor_item})
        if (*name) entry.bytes += string_bytes((**name).key);
    auto& cache = list_cache();
    if (entry.bytes > cache.stats.limit) return;
    while (cache.stats.bytes + entry.bytes > cache.stats.limit && !cache.entries.empty()) {
        cache.stats.bytes -= cache.entries.front().bytes;
        cache.entries.pop_front(); ++cache.stats.evictions;
    }
    const auto bytes = entry.bytes;
    cache.entries.push_back(std::move(entry));
    cache.stats.bytes += bytes;
} catch (...) { /* Cache admission failure never prevents navigation. */ }
std::optional<Pane::CachedList> Pane::take_list_cache(const managed_list::Request& request) {
    auto& cache = list_cache();
    const auto found = std::find_if(cache.entries.begin(), cache.entries.end(), [&](const auto& entry) {
        return entry.owner == this && entry.snapshot.request.binding.group_id == request.binding.group_id &&
            entry.snapshot.request.binding.tab_id == request.binding.tab_id && entry.snapshot.resolved_location.value_or(entry.snapshot.request.location) == request.location;
    });
    if (found == cache.entries.end()) return {};
    auto entry = std::move(*found);
    cache.stats.bytes -= entry.bytes;
    cache.entries.erase(found); ++cache.stats.hits;
    return entry;
}
void Pane::cancel_managed_list() noexcept {
    cancel_list_rename();
    prune_list_cache(true);
    if (list_source_) list_source_->cancel();
    cancel_list_icons();
    clear_list_index();
    list_ = {};
    property_sort_pending_ = false;
    property_capacity_failed_ = false;
    property_sort_key_.clear();
    list_navigation_pending_ = false;
    list_history_target_.reset();
    list_retry_history_target_.reset();
    list_name_bytes_ = 0;
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

void Pane::sort_managed_list_rows() noexcept {
    cancel_list_rename();
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
    std::sort(list_.rows.begin(), list_.rows.end(), less);
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
    for (int index = 0; index < 5; ++index) {
        HDITEMW item{};
        item.mask = HDI_FORMAT;
        if (!SendMessageW(header, HDM_GETITEMW, static_cast<WPARAM>(index),
                          reinterpret_cast<LPARAM>(&item))) continue;
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (index == actual_column)
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
                list_name_bytes_ -= managed_list::owned_string_bytes(row.properties[2].text);
                row.properties[2] = {};
                list_name_bytes_ += managed_list::owned_string_bytes(row.properties[2].text);
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
        tab->sort_ascending = true;
    }
    apply_managed_list_sort();
    if (pane_host()) pane_host()->schedule_session_save();
}
HRESULT Pane::start_managed_list(const core::ShellLocation& location,
                                NavigationGeneration generation) {
    if (!active() || !active_tab() || !navigation_request_is_current(generation)) return E_ABORT;
    if (!list_source_) list_source_ = managed_list::make_local_source();
    if (list_source_) list_source_->cancel();
    cancel_list_icons();
    property_sort_cursor_ = 0;
    property_sort_key_ = active_tab()->sort_column;
    property_sort_pending_ = expensive_sort_slot() >= 0;
    property_capacity_failed_ = false;
    clear_list_index();
    auto cached = take_list_cache({content_request(), location});
    if (!content_request_is_current(list_.request.binding) &&
        list_.request.binding.group_id == pane_host()->active_group_id() &&
        list_.request.binding.tab_id == active_tab()->id &&
        location == list_.resolved_location.value_or(list_.request.location) &&
        !list_.updating && (list_.phase == managed_list::Phase::complete ||
                           list_.phase == managed_list::Phase::empty)) {
        // Explicit Refresh retains the active snapshot just like a cache hit.
        cached.emplace(CachedList{this, {}, capture_managed_list_interaction(), 0});
        cached->snapshot = std::move(list_);
    }
    save_list_cache();
    list_name_bytes_ = 0;
    list_ = { {content_request(), location}, {}, managed_list::Phase::loading, 0};
    list_.notification = managed_list::is_filesystem_location(location.parsing_name)
        ? managed_list::NotificationCapability::filesystem_watch
        : managed_list::NotificationCapability::manual_refresh;
    list_navigation_pending_ = true;
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
        for (auto& row : list_.rows) {
            row.properties = {};
            list_name_bytes_ += managed_list::owned_row_bytes(row);
        }
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
        clear_list_index();
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
    if (!list_source_ || !active() ||
        (reentry_guard_ && reentry_guard().in_shell_call())) return;
    std::optional<ManagedListInteraction> interaction;
    bool rows_changed = false;
    // At most two batches per dispatch. Leave the event signalled if more
    // arrived, and let the main loop dispatch input before consuming again.
    for (std::size_t n = 0; n < managed_list::kQueuedBatches; ++n) {
        auto batch = list_source_->take();
        if (!batch) break;
        if (!active() || !content_request_is_current(batch->request.binding) ||
            !(batch->request == list_.request)) continue;
        cancel_list_rename(); // accepted batches invalidate the native edit row
        if (!bounded_location(batch->resolved_location) || !bounded_location(batch->parent_location) ||
            (batch->parent_outcome == managed_list::ParentOutcome::value) != batch->parent_location.has_value()) {
            list_.phase = managed_list::Phase::error;
            list_.error = ERROR_INVALID_DATA;
            list_.updating = false;
            clear_list_index();
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
            if (list_name_bytes_ - old_bytes + bytes + list_.rows.capacity() * sizeof(managed_list::Row) >
                managed_list::kMaxRowBytes) {
                list_.phase = managed_list::Phase::limited;
                list_.updating = false;
                clear_list_index();
                list_source_->cancel();
                resolve_list_navigation(false);
                break;
            }
            list_name_bytes_ = list_name_bytes_ - old_bytes + bytes;
            list_.resolved_location = std::move(resolved);
            list_.parent_location = std::move(parent);
            list_.parent_outcome = batch->parent_outcome;
            list_.notification = batch->notification;
        }
        if (batch->change != managed_list::Change::none) {
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
            clear_list_index();
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
                clear_list_index();
            }
            if (terminal) {
                list_.updating = false;
                // Cache reconciliation is not a notification subscription.
                // UNC/mapped/provider locations may never send watching.
                if (list_.watch == managed_list::WatchState::watching)
                    list_.watch = managed_list::WatchState::inactive;
            }
            rows_changed = rows_changed || terminal;
            continue;
        }
        const auto count = std::min(batch->rows.size(), managed_list::kMaxRows-list_.rows.size());
        if (count != 0 && !interaction)
            interaction.emplace(capture_managed_list_interaction());
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
            const auto bytes = managed_list::owned_row_bytes(batch->rows[accepted]);
            if (list_name_bytes_ + bytes + list_.rows.capacity()*sizeof(managed_list::Row) >
                managed_list::kMaxRowBytes) break;
            list_name_bytes_ += bytes;
            list_.rows.push_back(std::move(batch->rows[accepted]));
        }
        rows_changed = rows_changed || accepted != 0;
        list_.phase = accepted < batch->rows.size() ? managed_list::Phase::limited : batch->phase;
        list_.error = batch->error;
        if (list_.phase == managed_list::Phase::error || list_.phase == managed_list::Phase::limited)
            list_source_->cancel();
    }
    if (rows_changed) {
        cancel_list_icons();
        if (list_.watch != managed_list::WatchState::recovering) sort_managed_list_rows();
    }
    update_managed_list();
    if (rows_changed) {
        if (list_window_ && !list_.rows.empty())
            ListView_RedrawItems(list_window_, 0,
                static_cast<int>(list_.rows.size()) - 1);
        if (interaction) restore_managed_list_interaction(*interaction);
    }
    update_managed_list_sort_indicator();
    finish_property_batch();
    queue_list_icon_viewport();
} catch (...) {
    if (list_source_) list_source_->cancel();
    clear_list_index();
    list_.phase = managed_list::Phase::error;
    list_.error = ERROR_NOT_ENOUGH_MEMORY;
    list_.updating = false;
    resolve_list_navigation(false);
    update_managed_list();
}
void Pane::clear_list_index() noexcept {
    recovery_index_.clear();
    decltype(recovery_seen_) empty{IndexAllocator<unsigned char>{&index_memory_}};
    recovery_seen_.swap(empty);
}
bool Pane::index_list_row(std::size_t index) noexcept try {
    recovery_index_.insert(index);
    return true;
} catch (...) {
    // The allocator rejects before exceeding its separate 4 MiB ceiling. Keep
    // the sole row snapshot, stop automatic work and require explicit Refresh.
    clear_list_index();
    list_.watch = managed_list::WatchState::failed;
    list_.watch_error = ERROR_NOT_ENOUGH_MEMORY;
    if (list_source_) list_source_->cancel();
    return false;
}
void Pane::apply_list_changes(managed_list::Batch& batch, ManagedListInteraction& interaction) {
    using managed_list::Change;
    using managed_list::WatchState;
    if (batch.change == Change::watch_error) {
        list_.watch = WatchState::failed; list_.watch_error = batch.error;
        clear_list_index(); return;
    }
    if (batch.change == Change::watching) {
        list_.watch = WatchState::watching; list_.watch_error = 0; return;
    }
    if (batch.change == Change::rescan_begin) {
        clear_list_index();
        recovery_work_ = {};
        list_.watch = WatchState::recovering;
        try {
            // Fixed mark storage shares the SAME 4 MiB allocator ceiling.
            // No per-row padding is added to the authoritative row snapshot.
            recovery_seen_.assign(managed_list::kMaxRows, 0);
        } catch (...) {
            list_.watch = WatchState::failed; list_.watch_error = ERROR_NOT_ENOUGH_MEMORY;
            list_source_->cancel(); clear_list_index(); return;
        }
        for (std::size_t i = 0; i < list_.rows.size(); ++i) {
            if (!index_list_row(i)) return;
            ++recovery_work_.indexed_rows;
        }
        return;
    }
    if (batch.rows.size() > managed_list::kBatchRows ||
        batch.removed.size() > managed_list::kBatchRows || batch.renamed.size() > managed_list::kBatchRows) {
        list_.watch = WatchState::failed; list_.watch_error = ERROR_INVALID_DATA;
        list_source_->cancel(); clear_list_index(); return;
    }
    const auto row_bytes = managed_list::owned_row_bytes;
    const auto translate = [&](std::optional<managed_list::ItemIdentity>& name, const auto& from, const auto& to) {
        if (name && *name == from) name = to;
    };
    for (const auto& [old_leaf, new_leaf] : batch.renamed) {
        const managed_list::ItemIdentity from{old_leaf, false}, to{new_leaf, false};
        // A rename is the only notification that transfers an interaction
        // identity. Deletes never implicitly select a different row.
        for (auto& name : interaction.selected_items) if (name == from) name = to;
        translate(interaction.focused_item, from, to);
        translate(interaction.anchor_item, from, to);
        translate(interaction.viewport_anchor_item, from, to);
        std::erase_if(list_.rows, [&](const auto& row) {
            if (row.identity != from) return false;
            list_name_bytes_ -= row_bytes(row); return true;
        });
    }
    std::sort(interaction.selected_items.begin(), interaction.selected_items.end(),
        [](const auto& a, const auto& b) { return a < b; });
    if (!batch.removed.empty()) std::erase_if(list_.rows, [&](const auto& row) {
        if (row.identity.shell_item || std::find(batch.removed.begin(), batch.removed.end(),
                row.identity.key) == batch.removed.end()) return false;
        list_name_bytes_ -= row_bytes(row); return true;
    });
    // Recovery builds this index exactly once. No erasing/reordering until its
    // terminal batch: stable integer keys survive row-vector reallocations.
    // Ordinary deltas build once per coalesced batch, never once per row.
    if (batch.change != Change::rescan) {
        clear_list_index();
        for (std::size_t i = 0; i < list_.rows.size(); ++i)
            if (!index_list_row(i)) return;
    }
    for (auto& row : batch.rows) {
        if (!managed_list::prepare_row(row)) {
            list_.watch = WatchState::failed; list_.watch_error = ERROR_INVALID_DATA;
            list_source_->cancel(); clear_list_index(); return;
        }
        if (batch.change == Change::rescan) ++recovery_work_.lookups;
        const auto match = recovery_index_.find(row.identity);
        if (match != recovery_index_.end()) {
            auto* found = &list_.rows[*match];
            // Recovery can keep known metadata for unchanged rows; ordinary
            // notifications invalidate properties even if size/time are equal.
            const bool unchanged = batch.change == Change::rescan && found->bytes == row.bytes &&
                found->date_modified == row.date_modified && found->directory == row.directory &&
                found->name == row.name && found->child_location == row.child_location &&
                found->cloud == row.cloud;
            if (!unchanged) {
                const auto old_bytes = row_bytes(*found);
                const auto new_bytes = row_bytes(row);
                if (list_name_bytes_ - old_bytes + new_bytes + list_.rows.capacity() * sizeof(managed_list::Row) >
                    managed_list::kMaxRowBytes) {
                    list_.watch = WatchState::failed; list_.watch_error = ERROR_NOT_ENOUGH_MEMORY;
                    list_source_->cancel(); clear_list_index(); return;
                }
                list_name_bytes_ = list_name_bytes_ - old_bytes + new_bytes;
                *found = std::move(row);
            }
            if (batch.change == Change::rescan) recovery_seen_[*match] = 1;
        } else {
            const auto bytes = row_bytes(row);
            if (list_.rows.size() == managed_list::kMaxRows ||
                list_name_bytes_ + bytes + std::max(list_.rows.capacity(), list_.rows.size() + 1) *
                    sizeof(managed_list::Row) > managed_list::kMaxRowBytes) {
                list_.phase = managed_list::Phase::limited;
                list_.watch = WatchState::failed; list_.watch_error = ERROR_NOT_ENOUGH_MEMORY;
                list_source_->cancel(); clear_list_index(); return;
            }
            if (list_.rows.size() == list_.rows.capacity()) {
                const auto capacity = std::min(managed_list::kMaxRows, std::max<std::size_t>(
                    list_.rows.size() + 1, list_.rows.capacity() * 2));
                // Reserve headroom for owned strings as well as Row slots.
                // Filling all remaining bytes with empty slots would make the
                // very next recovery append fail despite ample usable budget.
                const auto average = (list_name_bytes_ + bytes + list_.rows.size()) / (list_.rows.size() + 1);
                const auto available = std::max(list_.rows.size() + 1,
                    managed_list::kMaxRowBytes / (sizeof(managed_list::Row) + average));
                list_.rows.reserve(std::min(capacity, available));
            }
            list_name_bytes_ += bytes;
            list_.rows.push_back(std::move(row));
            if (!index_list_row(list_.rows.size() - 1)) return;
            if (batch.change == Change::rescan) recovery_seen_[list_.rows.size() - 1] = 1;
        }
        if (batch.change == Change::rescan) ++recovery_work_.accounted_rows;
    }
    if (batch.change == Change::rescan && batch.phase != managed_list::Phase::loading) {
        if (batch.phase == managed_list::Phase::complete || batch.phase == managed_list::Phase::empty) {
            recovery_index_.clear(); // keys must be gone before compaction/sort
            std::size_t index = 0;
            std::erase_if(list_.rows, [&](const auto& row) {
                ++recovery_work_.accounted_rows;
                if (recovery_seen_[index++]) return false;
                list_name_bytes_ -= row_bytes(row); return true;
            });
            list_.watch = WatchState::watching;
        } else {
            // Never purge unseen rows after an incomplete recovery.
            list_.watch = WatchState::failed;
            list_.watch_error = batch.error ? batch.error : ERROR_BUFFER_OVERFLOW;
            list_source_->cancel();
        }
    }
    if (list_.watch != WatchState::recovering) clear_list_index();
    list_.phase = list_.rows.empty() ? managed_list::Phase::empty : managed_list::Phase::complete;
    property_sort_pending_ = expensive_sort_slot() >= 0;
    // Reuse image slots on the next visible demand, including overlay-only
    // changes for which the basic size/time cache key did not change.
    icon_cache_.clear();
    if (icon_images_) ImageList_SetImageCount(icon_images_, 2);
}
void Pane::update_managed_list() noexcept try {
    if (list_window_ && ListView_GetItemCount(list_window_) !=
            static_cast<int>(list_.rows.size()))
        ListView_SetItemCountEx(list_window_, static_cast<int>(list_.rows.size()),
            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    std::wstring text = std::to_wstring(list_.rows.size()) + L" items";
    switch (list_.phase) {
    case managed_list::Phase::loading: text += list_.updating ? L" - Updating..." : L" - Loading..."; break;
    case managed_list::Phase::empty: text = L"This folder is empty."; break;
    case managed_list::Phase::error:
        text += list_.error == ERROR_NO_SYSTEM_RESOURCES
            ? L" - Worker capacity exhausted. Leave this location or Refresh to retry."
            : L" - Cannot read this location. Refresh to retry. Error " + std::to_wstring(list_.error);
        break;
    case managed_list::Phase::limited: text += L" - Row or memory limit reached (100,000 rows / 24 MiB). Open a subfolder to continue."; break;
    default: break;
    }
    if (list_.notification == managed_list::NotificationCapability::manual_refresh)
        text += L" - Automatic refresh unavailable. Use Refresh to update.";
    if (list_.watch == managed_list::WatchState::recovering) text += L" - Recovering changes...";
    if (list_.watch == managed_list::WatchState::failed)
        text += L" - Automatic refresh unavailable. Refresh to retry. Error " + std::to_wstring(list_.watch_error);
    if (property_sort_pending_) text += L" - Sorting properties...";
    if (property_capacity_failed_) text += L" - Properties unavailable. Refresh to retry.";
    if (status_bar_) SetWindowTextW(status_bar_, text.c_str());
} catch (...) { if (status_bar_) SetWindowTextW(status_bar_, L"Cannot display folder status."); }
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
        !content_request_is_current(list_.request.binding) || !list_.rows[row].directory) return false;
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
    if (notification->code == LVN_BEGINDRAG) { (void)begin_list_drag(); return 0; }
    if (auto card = draw_list_card(notification)) return card;
    if (notification->code == LVN_GETDISPINFOW) {
        auto* info = reinterpret_cast<NMLVDISPINFOW*>(notification);
        if (info->item.mask & LVIF_IMAGE)
            info->item.iImage = info->item.iItem < 0 ? 0 :
                list_item_image(static_cast<std::size_t>(info->item.iItem));
        if ((info->item.mask & LVIF_TEXT) && info->item.iItem >= 0 &&
            static_cast<std::size_t>(info->item.iItem) < list_.rows.size()) {
            const auto& row = list_.rows[info->item.iItem];
            std::wstring text;
            if (info->item.iSubItem == 0) text = row.name;
            else if (info->item.iSubItem == 1)
                text = row.directory ? L"" : managed_list::file_size_text(row.bytes); // Explorer leaves folder sizes blank
            else if (info->item.iSubItem == 2)
                text = format_modified_time(row.date_modified);
            else if (info->item.iSubItem == 5) text = row.cloud.text();
            else if (info->item.iSubItem == 3 || info->item.iSubItem == 4) {
                const auto& value = row.properties[info->item.iSubItem - 3];
                if (value.state == managed_list::PropertyState::unresolved)
                    text = property_capacity_failed_ ? L"Unavailable" : L"Loading...";
                else if (value.state == managed_list::PropertyState::failed) text = L"Unavailable";
                else if (value.state == managed_list::PropertyState::value) {
                    if (info->item.iSubItem == 4 && value.numeric) {
                        const auto seconds = static_cast<std::uint64_t>(value.number / 10000000);
                        text = std::to_wstring(seconds / 60) + L":" +
                            (seconds % 60 < 10 ? L"0" : L"") + std::to_wstring(seconds % 60);
                    } else text = value.numeric ? std::to_wstring(static_cast<double>(value.number)) : value.text;
                }
            }
            if (info->item.cchTextMax > 0)
                lstrcpynW(info->item.pszText, text.c_str(), info->item.cchTextMax);
        }
        return 0;
    }
    if (notification->code == LVN_COLUMNCLICK) {
        const auto* click = reinterpret_cast<const NMLISTVIEW*>(notification);
        switch (click->iSubItem) {
            case 0: set_managed_list_sort(panedock::shell_core::SortProperty::name); break;
            case 1: set_managed_list_sort(panedock::shell_core::SortProperty::size); break;
            case 2: set_managed_list_sort(panedock::shell_core::SortProperty::date_modified); break;
            case 3: case 4: {
                auto* tab = active_tab();
                if (!tab) break;
                const std::string key = click->iSubItem == 3 ? "System.ItemTypeText" : "System.Media.Duration";
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
