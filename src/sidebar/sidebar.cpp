#include "sidebar/sidebar.h"

#include <algorithm>
#include <utility>

namespace panedock::sidebar {

bool Sidebar::create(HWND parent, int control_id) noexcept {
    parent_ = parent;
    control_id_ = control_id;
    list_box_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_HASSTRINGS |
            LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | LBS_OWNERDRAWFIXED,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(control_id),
        GetModuleHandleW(nullptr), nullptr);
    if (list_box_ != nullptr) {
        SendMessageW(list_box_, WM_SETFONT,
                     reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                     TRUE);
    }
    return list_box_ != nullptr;
}

void Sidebar::set_rect(const RECT& rect, UINT dpi) noexcept {
    if (list_box_ == nullptr) return;
    dpi_ = dpi;
    SetWindowPos(list_box_, nullptr, rect.left, rect.top,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    const int item_height = std::max(1, MulDiv(28, static_cast<int>(dpi), 96));
    SendMessageW(list_box_, LB_SETITEMHEIGHT, 0, item_height);
}

void Sidebar::set_groups(const std::vector<GroupSummary>& groups) {
    groups_ = groups;
    SendMessageW(list_box_, LB_RESETCONTENT, 0, 0);
    for (const auto& group : groups_) {
        SendMessageW(list_box_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(group.name.c_str()));
    }
}

std::optional<std::size_t> Sidebar::selected_index() const noexcept {
    const LRESULT selected = SendMessageW(list_box_, LB_GETCURSEL, 0, 0);
    return selected == LB_ERR
               ? std::nullopt
               : std::optional{static_cast<std::size_t>(selected)};
}

void Sidebar::set_selected_index(std::size_t index) noexcept {
    SendMessageW(list_box_, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

bool Sidebar::measure_item(MEASUREITEMSTRUCT* item, UINT dpi) const noexcept {
    if (item == nullptr || item->CtlType != ODT_LISTBOX ||
        item->CtlID != static_cast<UINT>(control_id_)) return false;
    item->itemHeight = static_cast<UINT>(
        std::max(1, MulDiv(28, static_cast<int>(dpi), 96)));
    return true;
}

bool Sidebar::draw_item(const DRAWITEMSTRUCT* item) const noexcept {
    if (item == nullptr || item->CtlType != ODT_LISTBOX ||
        item->CtlID != static_cast<UINT>(control_id_)) return false;
    if (item->itemID == static_cast<UINT>(-1) ||
        item->itemID >= groups_.size()) return true;

    const bool selected = (item->itemState & ODS_SELECTED) != 0;
    FillRect(item->hDC, &item->rcItem,
             GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC,
                 GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    RECT text_rect = item->rcItem;
    text_rect.left += MulDiv(8, static_cast<int>(dpi_), 96);
    DrawTextW(item->hDC, groups_[item->itemID].name.c_str(), -1, &text_rect,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    if ((item->itemState & ODS_FOCUS) != 0) DrawFocusRect(item->hDC, &item->rcItem);
    return true;
}

bool Sidebar::begin_rename() {
    if (editor_ != nullptr) return false;
    const auto selected = selected_index();
    if (!selected.has_value() || *selected >= groups_.size()) return false;

    RECT rect{};
    if (SendMessageW(list_box_, LB_GETITEMRECT, *selected,
                     reinterpret_cast<LPARAM>(&rect)) == LB_ERR) return false;
    MapWindowPoints(list_box_, parent_, reinterpret_cast<POINT*>(&rect), 2);
    editor_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", groups_[*selected].name.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, rect.left,
        rect.top, rect.right - rect.left, rect.bottom - rect.top, parent_,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    if (editor_ == nullptr) return false;
    SendMessageW(editor_, WM_SETFONT, SendMessageW(list_box_, WM_GETFONT, 0, 0),
                 TRUE);
    SetWindowLongPtrW(editor_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    original_edit_proc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        editor_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Sidebar::edit_proc)));
    SetFocus(editor_);
    SendMessageW(editor_, EM_SETSEL, 0, -1);
    return true;
}

std::optional<std::wstring> Sidebar::take_rename_text() noexcept {
    return std::exchange(pending_rename_, std::nullopt);
}

LRESULT CALLBACK Sidebar::edit_proc(HWND window, UINT message, WPARAM wparam,
                                    LPARAM lparam) {
    auto* self = reinterpret_cast<Sidebar*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self != nullptr && message == WM_KEYDOWN && wparam == VK_RETURN) {
        self->close_editor(true);
        return 0;
    }
    if (self != nullptr && message == WM_KEYDOWN && wparam == VK_ESCAPE) {
        self->close_editor(false);
        return 0;
    }
    if (self != nullptr && message == WM_KILLFOCUS) {
        self->close_editor(false);
        return 0;
    }
    return CallWindowProcW(self != nullptr ? self->original_edit_proc_
                                           : DefWindowProcW,
                           window, message, wparam, lparam);
}

void Sidebar::close_editor(bool commit) {
    if (editor_ == nullptr) return;
    if (commit) {
        const int length = GetWindowTextLengthW(editor_);
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(editor_, text.data(), length + 1);
        text.resize(static_cast<std::size_t>(length));
        pending_rename_ = std::move(text);
    }
    const HWND editor = std::exchange(editor_, nullptr);
    DestroyWindow(editor);
    original_edit_proc_ = nullptr;
    if (commit) SendMessageW(parent_, kRenameCommitMessage, 0, 0);
}

}  // namespace panedock::sidebar
