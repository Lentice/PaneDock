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
    // Stored widths are 96-DPI pixels: a DPI change re-applies them at the new scale.
    if (!column_ids_.empty() && tab->columns == applied_columns_ && paint_dpi_ == applied_columns_dpi_) return;
    const auto layout = core::parse_columns(tab->columns);
    while (ListView_DeleteColumn(list_window_, 0)) {}
    column_ids_.clear();
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
    for (const auto& spec : layout) {
        const auto* info = core::find_column(spec.id);
        if (!info) continue; // a newer build's column: kept in the setting, not shown here
        column.pszText = const_cast<wchar_t*>(info->label);
        column.cx = core::column_width_to_pixels(spec.width, paint_dpi_);
        column.iSubItem = static_cast<int>(column_ids_.size()); // callbacks report this position
        column.fmt = info->right_aligned ? LVCFMT_RIGHT : LVCFMT_LEFT;
        if (ListView_InsertColumn(list_window_, static_cast<int>(column_ids_.size()), &column) < 0) continue;
        column_ids_.push_back(spec.id);
    }
    applied_columns_ = tab->columns;
    applied_columns_dpi_ = paint_dpi_;
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
        layout.push_back({column_ids_[index],
            core::column_width_from_pixels(ListView_GetColumnWidth(list_window_, index), applied_columns_dpi_)});
    auto captured = core::captured_columns(tab->columns, std::move(layout));
    if (!captured) return;
    const auto text = std::move(*captured);
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
    if (!active() || !active_tab() || !list_window_ ||
        (reentry_guard_ && reentry_guard().in_shell_call())) return;
    capture_list_columns();
    const auto request = content_request();
    const auto layout = core::parse_columns(active_tab()->columns);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (const auto* info : core::column_menu_order()) {
        UINT flags = MF_STRING;
        if (core::column_visible(layout, info->id)) flags |= MF_CHECKED;
        if (info->id == "name") flags |= MF_GRAYED; // always shown
        const auto index = static_cast<std::size_t>(info - core::kKnownColumns.data());
        AppendMenuW(menu, flags, static_cast<UINT_PTR>(index + 1), info->label);
    }
    const HWND list = list_window_;
    int chosen = 0;
    {
        ShellCallScope shell_call(reentry_guard());
        chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y,
                                 list, nullptr);
    }
    DestroyMenu(menu);
    // The menu pumps messages: re-verify before touching anything.
    if (chosen <= 0 || static_cast<std::size_t>(chosen) > core::kKnownColumns.size() ||
        list_window_ != list || !IsWindow(list) || !content_request_is_current(request)) return;
    toggle_list_column(core::kKnownColumns[static_cast<std::size_t>(chosen - 1)].id);
} catch (...) {}

} // namespace panedock::app_shell
