#include "app_shell/pane.h"
#include "app_shell/pane_host.h"
#include "core/columns.h"
#include <windowsx.h>
#include <algorithm>

namespace panedock::app_shell {

namespace {
// Stable subitem for a column id: its position in kKnownColumns. comctl32 reports the
// header position in callbacks and notifications, so list_column_subitem() converts.
int subitem_of(std::string_view id) noexcept {
    const auto* info = core::find_column(id);
    return info ? static_cast<int>(info - core::kKnownColumns.data()) : -1;
}
}

int Pane::list_column_subitem(int header_index) const noexcept {
    if (header_index < 0 || static_cast<std::size_t>(header_index) >= column_ids_.size()) return -1;
    return subitem_of(column_ids_[header_index]);
}

int Pane::list_column_index(std::string_view id) const noexcept {
    const auto found = std::find(column_ids_.begin(), column_ids_.end(), id);
    return found == column_ids_.end() ? -1 : static_cast<int>(found - column_ids_.begin());
}

void Pane::apply_list_columns() noexcept try {
    const auto* tab = active_tab();
    if (!list_window_ || !tab) return;
    if (!column_ids_.empty() && tab->columns == applied_columns_) return;
    const auto layout = core::parse_columns(tab->columns);
    while (ListView_DeleteColumn(list_window_, 0)) {}
    column_ids_.clear();
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
    for (const auto& spec : layout) {
        const auto* info = core::find_column(spec.id);
        if (!info) continue; // a newer build's column: kept in the setting, not shown here
        column.pszText = const_cast<wchar_t*>(info->label);
        column.cx = spec.width;
        column.iSubItem = static_cast<int>(column_ids_.size()); // callbacks report this position
        column.fmt = info->right_aligned ? LVCFMT_RIGHT : LVCFMT_LEFT;
        if (ListView_InsertColumn(list_window_, static_cast<int>(column_ids_.size()), &column) < 0) continue;
        column_ids_.push_back(spec.id);
    }
    applied_columns_ = tab->columns;
    update_managed_list_sort_indicator();
} catch (...) {}

void Pane::capture_list_columns() noexcept try {
    auto* tab = active_tab();
    const int count = list_window_ ? Header_GetItemCount(ListView_GetHeader(list_window_)) : 0;
    if (!tab || count <= 0 || static_cast<std::size_t>(count) != column_ids_.size()) return;
    std::vector<int> order(static_cast<std::size_t>(count));
    if (!ListView_GetColumnOrderArray(list_window_, count, order.data())) return;
    // Name carries the icon and stays first, like Explorer.
    if (order[0] != 0) {
        order.erase(std::find(order.begin(), order.end(), 0));
        order.insert(order.begin(), 0);
        ListView_SetColumnOrderArray(list_window_, count, order.data());
    }
    std::vector<core::ColumnSpec> layout;
    for (const int index : order)
        layout.push_back({column_ids_[index], ListView_GetColumnWidth(list_window_, index)});
    // Columns this build does not know stay in the setting, after the ones it shows.
    for (const auto& previous : core::parse_columns(tab->columns))
        if (!core::column_visible(layout, previous.id) && !core::find_column(previous.id))
            layout.push_back(previous);
    const auto text = core::format_columns(layout);
    if (text == tab->columns || (tab->columns.empty() && text == core::format_columns(core::default_columns())))
        return;
    tab->columns = text;
    applied_columns_ = text; // already on screen
    if (pane_host()) pane_host()->schedule_session_save();
} catch (...) {}

void Pane::toggle_list_column(std::string_view id) noexcept try {
    auto* tab = active_tab();
    if (!tab || !list_window_) return;
    capture_list_columns(); // keep the widths and order the user has now
    auto layout = core::parse_columns(tab->columns);
    core::toggle_column(layout, id);
    tab->columns = core::format_columns(layout);
    apply_list_columns();
    // Sorting by a column that is no longer shown keeps working; only the arrow goes.
    queue_list_icon_viewport();
    if (pane_host()) pane_host()->schedule_session_save();
} catch (...) {}

void Pane::show_list_column_menu(POINT screen) noexcept try {
    auto* tab = active_tab();
    if (!tab || !list_window_ || !active()) return;
    capture_list_columns();
    const auto layout = core::parse_columns(tab->columns);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (std::size_t index = 0; index < core::kKnownColumns.size(); ++index) {
        const auto& info = core::kKnownColumns[index];
        UINT flags = MF_STRING;
        if (core::column_visible(layout, info.id)) flags |= MF_CHECKED;
        if (info.id == "name") flags |= MF_GRAYED; // always shown
        AppendMenuW(menu, flags, static_cast<UINT_PTR>(index + 1), info.label);
    }
    const HWND list = list_window_;
    const int chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y,
                                        list, nullptr);
    DestroyMenu(menu);
    // The menu pumps messages: re-verify before touching anything.
    if (chosen <= 0 || list_window_ != list || !IsWindow(list) || !active()) return;
    toggle_list_column(core::kKnownColumns[static_cast<std::size_t>(chosen - 1)].id);
} catch (...) {}

} // namespace panedock::app_shell
