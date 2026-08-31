#include "sidebar/sidebar.h"

#include <algorithm>
#include <cmath>
#include <ole2.h>
#include <utility>

namespace panedock::sidebar {

namespace {

constexpr COLORREF kSidebarBackground = RGB(251, 252, 254);
constexpr COLORREF kSidebarActiveBackground = RGB(234, 241, 255);
constexpr COLORREF kSidebarHoverBackground = RGB(242, 245, 248);
constexpr COLORREF kSidebarText = RGB(75, 85, 101);
constexpr COLORREF kSidebarActiveText = RGB(23, 75, 180);
constexpr COLORREF kPlaceholderBackground = RGB(238, 242, 246);
constexpr COLORREF kPlaceholderBorder = RGB(203, 213, 225);

std::wstring format_subtitle(std::size_t pane_count, std::size_t tab_count) {
    std::wstring text = std::to_wstring(pane_count);
    text += pane_count == 1 ? L" pane · " : L" panes · ";
    text += std::to_wstring(tab_count);
    text += tab_count == 1 ? L" tab" : L" tabs";
    return text;
}

HFONT create_ui_font(HDC dc, const LOGFONTW& fallback) noexcept {
    LOGFONTW requested = fallback;
    if (wcscpy_s(requested.lfFaceName, LF_FACESIZE, L"Segoe UI") != 0)
        return CreateFontIndirectW(&fallback);
    requested.lfCharSet = DEFAULT_CHARSET;
    requested.lfQuality = CLEARTYPE_QUALITY;

    HFONT font = CreateFontIndirectW(&requested);
    if (font == nullptr) return CreateFontIndirectW(&fallback);
    bool has_requested_face = true;
    if (dc != nullptr) {
        const HGDIOBJ old_font = SelectObject(dc, font);
        if (old_font != nullptr && old_font != HGDI_ERROR) {
            wchar_t actual_face[LF_FACESIZE]{};
            const int length = GetTextFaceW(dc, LF_FACESIZE, actual_face);
            has_requested_face =
                length > 0 && lstrcmpiW(actual_face, L"Segoe UI") == 0;
            SelectObject(dc, old_font);
        }
    }
    if (has_requested_face) return font;
    DeleteObject(font);
    return CreateFontIndirectW(&fallback);
}

}  // namespace

bool Sidebar::create(HWND parent, int control_id,
                     ::IDropTarget* drop_target) noexcept {
    parent_ = parent;
    control_id_ = control_id;
    list_box_ = CreateWindowExW(
        0, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_HASSTRINGS |
            LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | LBS_OWNERDRAWFIXED,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(control_id),
        GetModuleHandleW(nullptr), nullptr);
    if (list_box_ != nullptr) {
        if (drop_target == nullptr ||
            FAILED(RegisterDragDrop(list_box_, drop_target))) {
            DestroyWindow(list_box_);
            list_box_ = nullptr;
            return false;
        }
        drag_drop_registered_ = true;
    }
    return list_box_ != nullptr;
}

void Sidebar::revoke_drag_drop() noexcept {
    if (!drag_drop_registered_) return;
    RevokeDragDrop(list_box_);
    drag_drop_registered_ = false;
}

void Sidebar::set_rect(const RECT& rect, UINT dpi) noexcept {
    if (list_box_ == nullptr) return;
    dpi_ = dpi;
    SetWindowPos(list_box_, nullptr, rect.left, rect.top,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    const int item_height =
        std::max(1, MulDiv(kGroupRowHeight, static_cast<int>(dpi), 96));
    SendMessageW(list_box_, LB_SETITEMHEIGHT, 0, item_height);
}

void Sidebar::set_groups(const std::vector<GroupSummary>& groups) {
    groups_ = groups;
    hover_index_.reset();
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

void Sidebar::set_hover_index(std::optional<std::size_t> index) noexcept {
    if (hover_index_ == index) return;
    hover_index_ = index;
    if (list_box_ != nullptr) InvalidateRect(list_box_, nullptr, FALSE);
}

bool Sidebar::measure_item(MEASUREITEMSTRUCT* item, UINT dpi) const noexcept {
    if (item == nullptr || item->CtlType != ODT_LISTBOX ||
        item->CtlID != static_cast<UINT>(control_id_)) return false;
    item->itemHeight = static_cast<UINT>(
        std::max(1, MulDiv(kGroupRowHeight, static_cast<int>(dpi), 96)));
    return true;
}

bool Sidebar::draw_item(
    const DRAWITEMSTRUCT* item, std::size_t group_index,
    bool placeholder) const noexcept {
    if (item == nullptr || item->CtlType != ODT_LISTBOX ||
        item->CtlID != static_cast<UINT>(control_id_)) return false;
    if (item->itemID == static_cast<UINT>(-1) ||
        group_index >= groups_.size()) return true;

    const bool selected = selected_index() == group_index;
    const bool hovered = hover_index_ == item->itemID;
    HBRUSH background = CreateSolidBrush(kSidebarBackground);
    if (background != nullptr) {
        FillRect(item->hDC, &item->rcItem, background);
        DeleteObject(background);
    }
    RECT pill = item->rcItem;
    pill.left += MulDiv(4, static_cast<int>(dpi_), 96);
    pill.right -= MulDiv(4, static_cast<int>(dpi_), 96);
    pill.top += MulDiv(2, static_cast<int>(dpi_), 96);
    pill.bottom -= MulDiv(2, static_cast<int>(dpi_), 96);
    const int radius = MulDiv(10, static_cast<int>(dpi_), 96);
    if (placeholder) {
        HBRUSH fill = CreateSolidBrush(kPlaceholderBackground);
        HPEN border = CreatePen(PS_DOT, 1, kPlaceholderBorder);
        if (fill != nullptr && border != nullptr) {
            const HGDIOBJ old_brush = SelectObject(item->hDC, fill);
            const HGDIOBJ old_pen = SelectObject(item->hDC, border);
            RoundRect(item->hDC, pill.left, pill.top, pill.right, pill.bottom,
                      radius, radius);
            SelectObject(item->hDC, old_pen);
            SelectObject(item->hDC, old_brush);
        }
        if (fill != nullptr) DeleteObject(fill);
        if (border != nullptr) DeleteObject(border);
    } else if (selected || hovered) {
        HBRUSH pill_brush = CreateSolidBrush(
            selected ? kSidebarActiveBackground : kSidebarHoverBackground);
        if (pill_brush != nullptr) {
            const HGDIOBJ old_brush = SelectObject(item->hDC, pill_brush);
            const HGDIOBJ old_pen =
                SelectObject(item->hDC, GetStockObject(NULL_PEN));
            RoundRect(item->hDC, pill.left, pill.top, pill.right, pill.bottom,
                      radius, radius);
            SelectObject(item->hDC, old_brush);
            SelectObject(item->hDC, old_pen);
            DeleteObject(pill_brush);
        }
    }

    const auto& group = groups_[group_index];

    RECT text_area = pill;
    text_area.left += MulDiv(6, static_cast<int>(dpi_), 96);
    text_area.right -= MulDiv(6, static_cast<int>(dpi_), 96);

    const int line_height = (text_area.bottom - text_area.top) / 2;
    RECT name_rect{text_area.left, text_area.top, text_area.right,
                   text_area.top + line_height};
    RECT subtitle_rect{text_area.left, name_rect.bottom, text_area.right,
                       text_area.bottom};

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    const bool have_system_font = SystemParametersInfoForDpi(
        SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_) != FALSE;
    HFONT name_font = nullptr;
    HFONT subtitle_font = nullptr;
    if (have_system_font) {
        LOGFONTW name_logfont = metrics.lfMessageFont;
        name_logfont.lfWeight = FW_SEMIBOLD;
        name_font = create_ui_font(item->hDC, name_logfont);

        LOGFONTW subtitle_logfont = metrics.lfMessageFont;
        subtitle_logfont.lfHeight = static_cast<LONG>(std::lround(
            static_cast<double>(subtitle_logfont.lfHeight) * 0.9));
        subtitle_logfont.lfWeight = FW_NORMAL;
        subtitle_font = create_ui_font(item->hDC, subtitle_logfont);
    }

    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC,
                 placeholder ? kPlaceholderContent
                             : selected ? kSidebarActiveText : kSidebarText);
    const HGDIOBJ old_name_font =
        name_font != nullptr ? SelectObject(item->hDC, name_font) : nullptr;
    DrawTextW(item->hDC, group.name.c_str(), -1, &name_rect,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    if (old_name_font != nullptr) SelectObject(item->hDC, old_name_font);

    const HGDIOBJ old_font =
        subtitle_font != nullptr ? SelectObject(item->hDC, subtitle_font)
                                 : nullptr;
    SetTextColor(item->hDC, kPlaceholderContent);
    const std::wstring subtitle =
        format_subtitle(group.pane_count, group.tab_count);
    DrawTextW(item->hDC, subtitle.c_str(), -1, &subtitle_rect,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    if (old_font != nullptr) SelectObject(item->hDC, old_font);
    if (name_font != nullptr) DeleteObject(name_font);
    if (subtitle_font != nullptr) DeleteObject(subtitle_font);
    if ((item->itemState & ODS_FOCUS) != 0) DrawFocusRect(item->hDC, &pill);
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
